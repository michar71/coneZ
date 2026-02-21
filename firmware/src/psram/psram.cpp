#include <Arduino.h>
#include <SPI.h>
#include <soc/spi_struct.h>
#include <freertos/semphr.h>
#include "driver/gpio.h"
#include "board.h"
#ifdef BOARD_HAS_NATIVE_PSRAM
#include "esp_spiram.h"
#endif
#include "psram.h"
#include "printManager.h"

// ---- Thread safety ----
// Recursive mutex protects SPI bus, cache, allocator, and fallback tracking.
// Recursive because public functions may nest (e.g. psram_read8 → psram_read,
// psram_free_all → psram_cache_flush).
static SemaphoreHandle_t psram_mutex = NULL;

static void psram_mutex_init(void) {
    if (!psram_mutex)
        psram_mutex = xSemaphoreCreateRecursiveMutex();
}

#define PSRAM_LOCK()    xSemaphoreTakeRecursive(psram_mutex, portMAX_DELAY)
#define PSRAM_UNLOCK()  xSemaphoreGiveRecursive(psram_mutex)

// ---- Fallback allocation tracking (system heap) ----
// Used when PSRAM is full, unavailable, or not present on the board.
// Tracks allocations so psram_free() and psram_free_all() can release them.
// If the table fills up, allocations still succeed but aren't tracked for free_all.

typedef struct { void *ptr; size_t size; } psram_fb_alloc_t;
static psram_fb_alloc_t psram_fb[PSRAM_ALLOC_ENTRIES];
static int psram_fb_num = 0;

// All psram_fb_* functions assume psram_mutex is held by the caller.

static uint32_t psram_fb_malloc(size_t size) {
    void *p = malloc(size);
    if (!p) return 0;
    if (psram_fb_num < PSRAM_ALLOC_ENTRIES) {
        psram_fb[psram_fb_num].ptr = p;
        psram_fb[psram_fb_num].size = size;
        psram_fb_num++;
    }
    return (uint32_t)p;
}

static bool psram_fb_free(uint32_t addr) {
    for (int i = 0; i < psram_fb_num; i++) {
        if ((uint32_t)psram_fb[i].ptr == addr) {
            free(psram_fb[i].ptr);
            psram_fb[i] = psram_fb[psram_fb_num - 1];
            psram_fb_num--;
            return true;
        }
    }
    return false;
}

static void psram_fb_free_all(void) {
    for (int i = 0; i < psram_fb_num; i++)
        free(psram_fb[i].ptr);
    psram_fb_num = 0;
}

// ======================================================================
#ifdef BOARD_HAS_IMPROVISED_PSRAM
// ======================================================================

#define PSRAM_CMD_READ       0x03
#define PSRAM_CMD_FAST_READ  0x0B
#define PSRAM_CMD_WRITE      0x02
#define PSRAM_CMD_RESET_EN   0x66
#define PSRAM_CMD_RESET      0x99
#define PSRAM_CMD_READ_ID    0x9F

#define PSRAM_SPI_FREQ_DEFAULT  40000000  // 40 MHz boot default (exact APB/2 divider)
#define PSRAM_SPI_FREQ_MAX      80000000  // ESP32-S3 FSPI bus max

// Virtual address offset: public API addresses = raw SPI address + this offset.
// Keeps addresses well above 0 so they can't be confused with NULL.
// Must stay below 0x3C000000 so IS_ADDRESS_MAPPED() still returns false.
#define PSRAM_ADDR_OFFSET    0x10000000UL

// Runtime SPI frequency and derived chunk sizes.
// Use slow read (0x03, no wait byte) at <= 33 MHz, fast read (0x0B, 1 wait byte) above.
static uint32_t psram_freq       = PSRAM_SPI_FREQ_DEFAULT;
static bool     psram_fast_read  = true;    // recalculated by psram_set_freq()
static int      psram_read_overhead = 5;    // recalculated by psram_set_freq()
static int      psram_read_chunk = 35;      // recalculated by psram_set_freq()
static int      psram_write_chunk = 36;     // recalculated by psram_set_freq()

// Buffers sized for max frequency (80 MHz = 80 bytes/CEM)
static constexpr int PSRAM_MAX_BYTES_PER_CEM = PSRAM_SPI_FREQ_MAX / 1000000;  // 80
static constexpr int PSRAM_MAX_READ_CHUNK  = PSRAM_MAX_BYTES_PER_CEM - 4;     // 76
static constexpr int PSRAM_MAX_WRITE_CHUNK = PSRAM_MAX_BYTES_PER_CEM - 4;     // 76

static SPIClass    spiPSRAM(FSPI);
static bool        psram_ok = false;

// Compute actual SPI clock: APB / ceil(APB / requested), same as hardware divider.
static uint32_t psram_actual_freq(uint32_t requested) {
    if (requested >= APB_CLK_FREQ) return APB_CLK_FREQ;
    uint32_t divider = (APB_CLK_FREQ + requested - 1) / requested;
    return APB_CLK_FREQ / divider;
}

// Recalculate chunk sizes and read mode for a given frequency.
// Called during setup (with full SPI reconfiguration) and at runtime.
// Caller must hold psram_mutex (or call before mutex-protected operations begin).
static void psram_set_freq(uint32_t freq_hz) {
    psram_freq = psram_actual_freq(freq_hz);
    psram_fast_read = (psram_freq > 33000000);
    psram_read_overhead = psram_fast_read ? 5 : 4;
    int bytes_per_cem = (psram_freq / 1000000) * 8 / 8;
    psram_read_chunk  = bytes_per_cem - psram_read_overhead;
    psram_write_chunk = bytes_per_cem - 4;
    // SPI clock is set by the caller — setup uses endTransaction/beginTransaction
    // (which takes the SPI bus mutex on loopTask), runtime uses setFrequency()
    // (which doesn't touch the bus mutex, safe from ShellTask).
}

// ---- Low-level helpers ----

static inline void cs_low()  { gpio_set_level((gpio_num_t)PSR_CE, 0); }
static inline void cs_high() { gpio_set_level((gpio_num_t)PSR_CE, 1); }

static void psram_cmd(uint8_t cmd) {
    cs_low();
    spiPSRAM.transfer(cmd);
    cs_high();
}

static void psram_reset() {
    psram_cmd(PSRAM_CMD_RESET_EN);
    delayMicroseconds(1);
    psram_cmd(PSRAM_CMD_RESET);
    delayMicroseconds(200);  // wait for reset + init
}

static uint16_t psram_read_id() {
    cs_low();
    spiPSRAM.transfer(PSRAM_CMD_READ_ID);
    spiPSRAM.transfer(0); spiPSRAM.transfer(0); spiPSRAM.transfer(0); // 24-bit addr
    uint8_t mfid = spiPSRAM.transfer(0);
    uint8_t kgd  = spiPSRAM.transfer(0);
    cs_high();
    return (mfid << 8) | kgd;
}

// ---- Core read/write (single chunk, bulk SPI transfer, respects tCEM) ----

// Read up to psram_read_chunk bytes in one CE# assertion.
// Uses transferBytes() for a single SPI FIFO transaction.
// At <= 33 MHz uses slow read (0x03, no wait); above uses fast read (0x0B, 1 wait byte).
static void psram_read_chunk_fn(uint32_t addr, uint8_t *buf, size_t len) {
    uint8_t tx[5 + PSRAM_MAX_READ_CHUNK] = {};  // sized for max freq
    uint8_t rx[5 + PSRAM_MAX_READ_CHUNK];
    tx[0] = psram_fast_read ? PSRAM_CMD_FAST_READ : PSRAM_CMD_READ;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8)  & 0xFF;
    tx[3] = addr & 0xFF;
    // If fast read, tx[4] is the wait byte (already zero)
    size_t total = psram_read_overhead + len;
    cs_low();
    spiPSRAM.transferBytes(tx, rx, total);
    cs_high();
    memcpy(buf, &rx[psram_read_overhead], len);
}

// Write up to psram_write_chunk bytes in one CE# assertion.
static void psram_write_chunk_fn(uint32_t addr, const uint8_t *buf, size_t len) {
    uint8_t tx[4 + PSRAM_MAX_WRITE_CHUNK];  // sized for max freq
    tx[0] = PSRAM_CMD_WRITE;
    tx[1] = (addr >> 16) & 0xFF;
    tx[2] = (addr >> 8)  & 0xFF;
    tx[3] = addr & 0xFF;
    memcpy(&tx[4], buf, len);
    size_t total = 4 + len;
    cs_low();
    spiPSRAM.transferBytes(tx, NULL, total);
    cs_high();
}

// ---- Internal bulk API (raw 0-based SPI addresses, loops over chunks) ----

static void psram_raw_read(uint32_t addr, uint8_t *buf, size_t len) {
    while (len > 0) {
        size_t n = (len > (size_t)psram_read_chunk) ? psram_read_chunk : len;
        psram_read_chunk_fn(addr, buf, n);
        addr += n; buf += n; len -= n;
    }
}

static void psram_raw_write(uint32_t addr, const uint8_t *buf, size_t len) {
    while (len > 0) {
        size_t n = (len > (size_t)psram_write_chunk) ? psram_write_chunk : len;
        psram_write_chunk_fn(addr, buf, n);
        addr += n; buf += n; len -= n;
    }
}

// ---- DRAM page cache (write-back, LRU eviction) ----

#define CACHE_TAG_EMPTY  0xFFFFFFFFUL  // Internal sentinel for empty cache slots

#if PSRAM_CACHE_PAGES > 0

static_assert((PSRAM_CACHE_PAGE_SIZE & (PSRAM_CACHE_PAGE_SIZE - 1)) == 0,
              "PSRAM_CACHE_PAGE_SIZE must be power of 2");

#define PSRAM_PAGE_MASK  (~((uint32_t)PSRAM_CACHE_PAGE_SIZE - 1))

typedef struct {
    uint32_t tag;        // Page-aligned raw PSRAM address, or CACHE_TAG_EMPTY
    uint32_t last_used;  // Monotonic counter for LRU
    bool     dirty;
    uint8_t  data[PSRAM_CACHE_PAGE_SIZE];
} psram_cache_line_t;

static psram_cache_line_t psram_cache[PSRAM_CACHE_PAGES];
static uint32_t cache_clock = 0;
static uint32_t cache_hit_count = 0;
static uint32_t cache_miss_count = 0;

static void psram_cache_init(void) {
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        psram_cache[i].tag = CACHE_TAG_EMPTY;
        psram_cache[i].last_used = 0;
        psram_cache[i].dirty = false;
    }
    cache_clock = 0;
    cache_hit_count = 0;
    cache_miss_count = 0;
}

// Find cached page or load it, evicting LRU victim if needed.
// Caller must hold psram_mutex.
static psram_cache_line_t *psram_cache_get(uint32_t page_addr) {
    // Search for hit
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        if (psram_cache[i].tag == page_addr) {
            psram_cache[i].last_used = ++cache_clock;
            cache_hit_count++;
            return &psram_cache[i];
        }
    }

    // Miss — find empty slot or LRU victim
    cache_miss_count++;
    int victim = 0;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        if (psram_cache[i].tag == CACHE_TAG_EMPTY) {
            victim = i;
            break;
        }
        if (psram_cache[i].last_used < oldest) {
            oldest = psram_cache[i].last_used;
            victim = i;
        }
    }

    // Evict: flush dirty page
    if (psram_cache[victim].tag != CACHE_TAG_EMPTY && psram_cache[victim].dirty)
        psram_raw_write(psram_cache[victim].tag, psram_cache[victim].data, PSRAM_CACHE_PAGE_SIZE);

    // Load new page
    psram_raw_read(page_addr, psram_cache[victim].data, PSRAM_CACHE_PAGE_SIZE);
    psram_cache[victim].tag = page_addr;
    psram_cache[victim].last_used = ++cache_clock;
    psram_cache[victim].dirty = false;
    return &psram_cache[victim];
}

void psram_cache_flush(void) {
    PSRAM_LOCK();
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        if (psram_cache[i].tag != CACHE_TAG_EMPTY && psram_cache[i].dirty) {
            psram_raw_write(psram_cache[i].tag, psram_cache[i].data, PSRAM_CACHE_PAGE_SIZE);
            psram_cache[i].dirty = false;
        }
    }
    PSRAM_UNLOCK();
}

void psram_cache_invalidate(void) {
    PSRAM_LOCK();
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        psram_cache[i].tag = CACHE_TAG_EMPTY;
        psram_cache[i].dirty = false;
    }
    PSRAM_UNLOCK();
}

uint32_t psram_cache_hits(void)   { return cache_hit_count; }
uint32_t psram_cache_misses(void) { return cache_miss_count; }

#else  // PSRAM_CACHE_PAGES == 0

static void psram_cache_init(void) {}
void     psram_cache_flush(void) {}
void     psram_cache_invalidate(void) {}
uint32_t psram_cache_hits(void)   { return 0; }
uint32_t psram_cache_misses(void) { return 0; }

#endif

// ---- Public bulk API (offset addresses, cache-aware, thread-safe) ----

void psram_read(uint32_t addr, uint8_t *buf, size_t len) {
    if (IS_ADDRESS_MAPPED(addr)) {
        memcpy(buf, (const void *)addr, len);
        return;
    }
    uint32_t raw = addr - PSRAM_ADDR_OFFSET;
    if (len > BOARD_PSRAM_SIZE || raw > BOARD_PSRAM_SIZE - len)
        return;  // out of bounds
    PSRAM_LOCK();
#if PSRAM_CACHE_PAGES > 0
    while (len > 0) {
        uint32_t page_addr = raw & PSRAM_PAGE_MASK;
        uint32_t page_off  = raw & (PSRAM_CACHE_PAGE_SIZE - 1);
        size_t n = PSRAM_CACHE_PAGE_SIZE - page_off;
        if (n > len) n = len;
        psram_cache_line_t *line = psram_cache_get(page_addr);
        memcpy(buf, &line->data[page_off], n);
        raw += n; buf += n; len -= n;
    }
#else
    psram_raw_read(raw, buf, len);
#endif
    PSRAM_UNLOCK();
}

void psram_write(uint32_t addr, const uint8_t *buf, size_t len) {
    if (IS_ADDRESS_MAPPED(addr)) {
        memcpy((void *)addr, buf, len);
        return;
    }
    uint32_t raw = addr - PSRAM_ADDR_OFFSET;
    if (len > BOARD_PSRAM_SIZE || raw > BOARD_PSRAM_SIZE - len)
        return;  // out of bounds
    PSRAM_LOCK();
#if PSRAM_CACHE_PAGES > 0
    while (len > 0) {
        uint32_t page_addr = raw & PSRAM_PAGE_MASK;
        uint32_t page_off  = raw & (PSRAM_CACHE_PAGE_SIZE - 1);
        size_t n = PSRAM_CACHE_PAGE_SIZE - page_off;
        if (n > len) n = len;
        psram_cache_line_t *line = psram_cache_get(page_addr);
        memcpy(&line->data[page_off], buf, n);
        line->dirty = true;
        raw += n; buf += n; len -= n;
    }
#else
    psram_raw_write(raw, buf, len);
#endif
    PSRAM_UNLOCK();
}

// ---- Typed accessors (little-endian, offset addresses) ----

uint8_t  psram_read8(uint32_t addr)  { uint8_t v;  psram_read(addr, &v, 1); return v; }
uint16_t psram_read16(uint32_t addr) { uint16_t v; psram_read(addr, (uint8_t*)&v, 2); return v; }
uint32_t psram_read32(uint32_t addr) { uint32_t v; psram_read(addr, (uint8_t*)&v, 4); return v; }
uint64_t psram_read64(uint32_t addr) { uint64_t v; psram_read(addr, (uint8_t*)&v, 8); return v; }

void psram_write8(uint32_t addr, uint8_t val)   { psram_write(addr, &val, 1); }
void psram_write16(uint32_t addr, uint16_t val)  { psram_write(addr, (uint8_t*)&val, 2); }
void psram_write32(uint32_t addr, uint32_t val)  { psram_write(addr, (uint8_t*)&val, 4); }
void psram_write64(uint32_t addr, uint64_t val)  { psram_write(addr, (uint8_t*)&val, 8); }

// ---- Free-list allocator ----
// Entries kept sorted by address in internal RAM. Adjacent free blocks merge on free().

#define PSRAM_ALIGN  4
#define PSRAM_ALIGN_UP(x)  (((x) + PSRAM_ALIGN - 1) & ~(PSRAM_ALIGN - 1))

typedef struct {
    uint32_t addr;
    uint32_t size;
    bool     used;      // true = allocated, false = free
} psram_block_t;

static psram_block_t psram_blocks[PSRAM_ALLOC_ENTRIES];
static int psram_block_count = 0;

static void psram_alloc_init(void) {
    memset(psram_blocks, 0, sizeof(psram_blocks));
    psram_blocks[0].addr = 0;
    psram_blocks[0].size = BOARD_PSRAM_SIZE;
    psram_blocks[0].used = false;
    psram_block_count = 1;
}

uint32_t psram_malloc(size_t size) {
    if (size == 0)
        return 0;

    PSRAM_LOCK();

    if (psram_ok) {
        size_t aligned = PSRAM_ALIGN_UP(size);

        // First-fit scan
        for (int i = 0; i < psram_block_count; i++) {
            if (psram_blocks[i].used || psram_blocks[i].size < aligned)
                continue;

            // Exact fit — just mark used
            if (psram_blocks[i].size == aligned) {
                psram_blocks[i].used = true;
                PSRAM_UNLOCK();
                return psram_blocks[i].addr + PSRAM_ADDR_OFFSET;
            }

            // Split: need room for one more entry
            if (psram_block_count >= PSRAM_ALLOC_ENTRIES)
                break;  // fall through to system malloc

            // Shift entries after i to make room for the remainder block
            memmove(&psram_blocks[i + 2], &psram_blocks[i + 1],
                    (psram_block_count - i - 1) * sizeof(psram_block_t));
            psram_block_count++;

            // Remainder (free) goes into slot i+1
            psram_blocks[i + 1].addr = psram_blocks[i].addr + aligned;
            psram_blocks[i + 1].size = psram_blocks[i].size - aligned;
            psram_blocks[i + 1].used = false;

            // Allocated block in slot i
            psram_blocks[i].size = aligned;
            psram_blocks[i].used = true;
            PSRAM_UNLOCK();
            return psram_blocks[i].addr + PSRAM_ADDR_OFFSET;
        }
    }

    // Fallback to system malloc (PSRAM full, out of entries, or not available)
    uint32_t result = psram_fb_malloc(size);
    PSRAM_UNLOCK();
    return result;
}

void psram_free(uint32_t addr) {
    if (addr == 0)
        return;

    PSRAM_LOCK();

    if (IS_ADDRESS_MAPPED(addr)) {
        if (!psram_fb_free(addr))
            free((void *)addr);  // not tracked (table was full), free directly
        PSRAM_UNLOCK();
        return;
    }

    uint32_t raw = addr - PSRAM_ADDR_OFFSET;

    // Find the block
    int i;
    for (i = 0; i < psram_block_count; i++) {
        if (psram_blocks[i].addr == raw && psram_blocks[i].used)
            break;
    }
    if (i >= psram_block_count) {
        PSRAM_UNLOCK();
        return;  // not found or not allocated
    }

    psram_blocks[i].used = false;

    // Merge with next block if free
    if (i + 1 < psram_block_count && !psram_blocks[i + 1].used) {
        psram_blocks[i].size += psram_blocks[i + 1].size;
        memmove(&psram_blocks[i + 1], &psram_blocks[i + 2],
                (psram_block_count - i - 2) * sizeof(psram_block_t));
        psram_block_count--;
    }

    // Merge with previous block if free
    if (i > 0 && !psram_blocks[i - 1].used) {
        psram_blocks[i - 1].size += psram_blocks[i].size;
        memmove(&psram_blocks[i], &psram_blocks[i + 1],
                (psram_block_count - i - 1) * sizeof(psram_block_t));
        psram_block_count--;
    }

    PSRAM_UNLOCK();
}

void psram_free_all(void) {
    PSRAM_LOCK();
    psram_cache_flush();
    psram_cache_invalidate();
    if (psram_ok)
        psram_alloc_init();
    psram_fb_free_all();
    PSRAM_UNLOCK();
}

size_t psram_bytes_used(void) {
    PSRAM_LOCK();
    size_t total = 0;
    for (int i = 0; i < psram_block_count; i++)
        if (psram_blocks[i].used)
            total += psram_blocks[i].size;
    PSRAM_UNLOCK();
    return total;
}

size_t psram_bytes_free(void) {
    PSRAM_LOCK();
    size_t total = 0;
    for (int i = 0; i < psram_block_count; i++)
        if (!psram_blocks[i].used)
            total += psram_blocks[i].size;
    PSRAM_UNLOCK();
    return total;
}

size_t psram_bytes_contiguous(void) {
    PSRAM_LOCK();
    size_t largest = 0;
    for (int i = 0; i < psram_block_count; i++)
        if (!psram_blocks[i].used && psram_blocks[i].size > largest)
            largest = psram_blocks[i].size;
    PSRAM_UNLOCK();
    return largest;
}

int psram_alloc_count(void) {
    PSRAM_LOCK();
    int n = 0;
    for (int i = 0; i < psram_block_count; i++)
        if (psram_blocks[i].used)
            n++;
    PSRAM_UNLOCK();
    return n;
}

int psram_alloc_entries_max(void) { return PSRAM_ALLOC_ENTRIES; }

// ---- Setup ----

int psram_setup(void) {
    psram_mutex_init();

    Serial.print("Init PSRAM... ");

    gpio_set_direction((gpio_num_t)PSR_CE, GPIO_MODE_OUTPUT);
    cs_high();

    // We own the FSPI bus exclusively — no other peripheral shares it.
    // beginTransaction configures the SPI correctly (SPISettings has a
    // special-case divider for >= APB/2 that spiFrequencyToClockDiv misses).
    // The transaction stays open — loopTask holds the bus lock permanently.
    // Runtime freq changes from ShellTask write the clock register directly.
    spiPSRAM.begin(PSR_SCK, PSR_MISO, PSR_MOSI, -1);  // no auto-CS
    psram_set_freq(PSRAM_SPI_FREQ_DEFAULT);
    spiPSRAM.beginTransaction(SPISettings(PSRAM_SPI_FREQ_DEFAULT, MSBFIRST, SPI_MODE0));

    psram_reset();

    uint16_t id = psram_read_id();
    uint8_t mfid = id >> 8;
    uint8_t kgd  = id & 0xFF;
    Serial.printf("MF=0x%02X KGD=0x%02X ", mfid, kgd);

    if (mfid != 0x0D) {
        Serial.println("FAIL — unexpected manufacturer ID");
        return -1;
    }
    if (kgd != 0x5D) {
        Serial.printf("WARNING — KGD=0x%02X (expected 0x5D for PASS die)\n", kgd);
    }

    // Quick sanity test: write/read a few raw addresses
    uint8_t pat[] = { 0xDE, 0xAD, 0xBE, 0xEF };
    uint8_t rbuf[4] = {};
    psram_raw_write(0x000000, pat, 4);
    psram_raw_read(0x000000, rbuf, 4);
    if (memcmp(pat, rbuf, 4) != 0) {
        Serial.println("FAIL — read-back mismatch");
        return -2;
    }

    // Also test a high address
    psram_raw_write(0x7FFFFC, pat, 4);
    psram_raw_read(0x7FFFFC, rbuf, 4);
    if (memcmp(pat, rbuf, 4) != 0) {
        Serial.println("FAIL — high-address read-back mismatch");
        return -3;
    }

    psram_alloc_init();
    psram_cache_init();
    psram_ok = true;
    Serial.println("OK (8 MB)");
    return 0;
}

// ---- Full memory test (diagnostic — no mutex, requires exclusive access) ----

int psram_test(bool forever) {
    if (!psram_ok) {
        printfnl(SOURCE_COMMANDS, "PSRAM not initialized\n");
        return -1;
    }
    if (psram_bytes_used() > 0 || psram_fb_num > 0) {
        printfnl(SOURCE_COMMANDS, "PSRAM test blocked — allocations exist\n");
        return -1;
    }

    psram_cache_invalidate();  // Test bypasses cache via raw SPI

    uint32_t size = BOARD_PSRAM_SIZE;
    uint32_t pass = 0;

    if (forever) {
        printfnl(SOURCE_COMMANDS, "PSRAM test forever: %u bytes, press any key to stop\n", size);
        while (Serial.available()) Serial.read();  // Drain stale input
    }

    do {
        uint32_t errors = 0;
        uint8_t wbuf[64], rbuf[64];
        uint8_t pass_xor = (uint8_t)pass;  // Vary pattern each pass

        printfnl(SOURCE_COMMANDS, "Pass %u: writing...\n", pass + 1);
        unsigned long t0 = micros();

        // Write phase: address-derived pattern XORed with pass number
        for (uint32_t addr = 0; addr < size; addr += 64) {
            for (int i = 0; i < 64; i++)
                wbuf[i] = (uint8_t)((addr + i) ^ ((addr + i) >> 8) ^ pass_xor);
            psram_raw_write(addr, wbuf, 64);
            if ((addr & 0xFFFF) == 0) {
                vTaskDelay(1);  // Feed task WDT every 64KB
                if (pass == 0 && (addr & 0xFFFFF) == 0)
                    printfnl(SOURCE_COMMANDS, "  Write: %u KB / %u KB\n", addr/1024, size/1024);
            }
        }

        unsigned long t_write = micros() - t0;

        printfnl(SOURCE_COMMANDS, "Pass %u: verifying...\n", pass + 1);
        unsigned long t1 = micros();

        // Verify phase
        for (uint32_t addr = 0; addr < size; addr += 64) {
            psram_raw_read(addr, rbuf, 64);
            for (int i = 0; i < 64; i++) {
                uint8_t expected = (uint8_t)((addr + i) ^ ((addr + i) >> 8) ^ pass_xor);
                if (rbuf[i] != expected) {
                    if (errors < 10)
                        printfnl(SOURCE_COMMANDS, "  MISMATCH at 0x%06X: wrote 0x%02X read 0x%02X\n",
                                 addr + i, expected, rbuf[i]);
                    errors++;
                }
            }
            if ((addr & 0xFFFF) == 0) {
                vTaskDelay(1);  // Feed task WDT every 64KB
                if (pass == 0 && (addr & 0xFFFFF) == 0)
                    printfnl(SOURCE_COMMANDS, "  Verify: %u KB / %u KB\n", addr/1024, size/1024);
            }
        }

        unsigned long t_read = micros() - t1;

        // Benchmark results
        uint32_t write_kbps = (t_write > 0) ? (uint32_t)((uint64_t)size * 1000 / t_write) : 0;
        uint32_t read_kbps  = (t_read  > 0) ? (uint32_t)((uint64_t)size * 1000 / t_read)  : 0;
        printfnl(SOURCE_COMMANDS, "Pass %u: write %u KB/s, read %u KB/s\n",
                 pass + 1, write_kbps, read_kbps);

        if (errors > 0) {
            printfnl(SOURCE_COMMANDS, "PSRAM test FAILED on pass %u: %u errors\n", pass + 1, errors);
            return -1;
        }

        printfnl(SOURCE_COMMANDS, "Pass %u: PASSED\n", pass + 1);
        pass++;

        // Check for user abort between passes
        if (forever && Serial.available()) {
            while (Serial.available()) Serial.read();  // Drain input
            printfnl(SOURCE_COMMANDS, "PSRAM test stopped by user after %u passes\n", pass);
            return 0;
        }

    } while (forever);

    return 0;
}

void psram_print_map(void) {
    if (!psram_ok) return;

    #define MAP_WIDTH 64
    char map[MAP_WIDTH + 1];
    uint32_t region_size = BOARD_PSRAM_SIZE / MAP_WIDTH;  // 128KB per char

    PSRAM_LOCK();
    for (int m = 0; m < MAP_WIDTH; m++) {
        uint32_t rstart = (uint32_t)m * region_size;
        uint32_t rend   = rstart + region_size;
        uint32_t used = 0;

        for (int i = 0; i < psram_block_count; i++) {
            if (!psram_blocks[i].used) continue;
            uint32_t bstart = psram_blocks[i].addr;
            uint32_t bend   = bstart + psram_blocks[i].size;
            if (bend <= rstart || bstart >= rend) continue;
            uint32_t os = (bstart > rstart) ? bstart : rstart;
            uint32_t oe = (bend < rend) ? bend : rend;
            used += oe - os;
        }

        if (used == 0)           map[m] = '-';
        else if (used >= region_size) map[m] = '*';
        else                     map[m] = '+';
    }
    PSRAM_UNLOCK();
    map[MAP_WIDTH] = '\0';

    printfnl(SOURCE_COMMANDS, F("  Map:       [%s]\n"), map);
    if (getAnsiEnabled())
        printfnl(SOURCE_COMMANDS, F("             \033[38;5;240m-\033[0m free  "
                   "\033[33m+\033[0m partial  "
                   "\033[31m*\033[0m full   "
                   "(128KB/char)\n"));
    else
        printfnl(SOURCE_COMMANDS, F("             - free  + partial  * full   (128KB/char)\n"));
    #undef MAP_WIDTH
}

void psram_print_cache_map(void) {
#if PSRAM_CACHE_PAGES > 0
    char map[PSRAM_CACHE_PAGES + 1];
    PSRAM_LOCK();
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        if (psram_cache[i].tag == CACHE_TAG_EMPTY) map[i] = '-';
        else if (psram_cache[i].dirty)             map[i] = 'D';
        else                                       map[i] = 'C';
    }
    PSRAM_UNLOCK();
    map[PSRAM_CACHE_PAGES] = '\0';
    printfnl(SOURCE_COMMANDS, F("  Cache map: [%s]\n"), map);
    if (getAnsiEnabled())
        printfnl(SOURCE_COMMANDS, F("             \033[38;5;240m-\033[0m empty  "
                   "\033[32mC\033[0m clean  "
                   "\033[31mD\033[0m dirty\n"));
    else
        printfnl(SOURCE_COMMANDS, F("             - empty  C clean  D dirty\n"));
#endif
}

void psram_print_cache_detail(void) {
#if PSRAM_CACHE_PAGES > 0
    uint32_t hits = psram_cache_hits(), misses = psram_cache_misses();
    uint32_t total = hits + misses;
    printfnl(SOURCE_COMMANDS, F("Cache: %d pages x %d bytes (%u KB DRAM)\n"),
             PSRAM_CACHE_PAGES, PSRAM_CACHE_PAGE_SIZE,
             (PSRAM_CACHE_PAGES * PSRAM_CACHE_PAGE_SIZE) / 1024);
    printfnl(SOURCE_COMMANDS, F("Hits:  %u / %u (%u%%)\n"),
             hits, total, total ? (hits * 100 / total) : 0);

    PSRAM_LOCK();
    int used = 0, dirty = 0;
    uint32_t max_used = 0;
    for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
        if (psram_cache[i].tag != CACHE_TAG_EMPTY) {
            used++;
            if (psram_cache[i].dirty) dirty++;
            if (psram_cache[i].last_used > max_used) max_used = psram_cache[i].last_used;
        }
    }
    printfnl(SOURCE_COMMANDS, F("Used:  %d / %d  (dirty: %d)\n"), used, PSRAM_CACHE_PAGES, dirty);
    printfnl(SOURCE_COMMANDS, F("Clock: %u\n\n"), cache_clock);

    if (used > 0) {
        printfnl(SOURCE_COMMANDS, F("Page  Address     Dirty  Age\n"));
        printfnl(SOURCE_COMMANDS, F("----  ----------  -----  --------\n"));
        for (int i = 0; i < PSRAM_CACHE_PAGES; i++) {
            if (psram_cache[i].tag == CACHE_TAG_EMPTY) continue;
            printfnl(SOURCE_COMMANDS, F("%3d   0x%08X  %-5s  %u\n"),
                     i, psram_cache[i].tag,
                     psram_cache[i].dirty ? "yes" : "no",
                     psram_cache[i].last_used);
        }
    }
    PSRAM_UNLOCK();
#else
    printfnl(SOURCE_COMMANDS, F("Cache disabled (PSRAM_CACHE_PAGES=0)\n"));
#endif
}

uint32_t psram_size(void) { return BOARD_PSRAM_SIZE; }
bool psram_available(void) { return psram_ok; }

uint32_t psram_get_freq(void) { return psram_freq; }

int psram_change_freq(uint32_t freq_hz) {
    if (!psram_ok) return -1;
    if (freq_hz < 5000000 || freq_hz > PSRAM_SPI_FREQ_MAX) return -1;
    PSRAM_LOCK();
    psram_cache_flush();
    psram_set_freq(freq_hz);
    // Write the SPI2 clock register directly via the SOC peripheral struct.
    // beginTransaction() in setup permanently holds both paramLock and the HAL
    // spi->lock on loopTask (endTransaction is never called — we own the bus
    // exclusively).  So we can't use any Arduino SPI API from ShellTask.
    // Direct register write is safe under psram_mutex.  GPSPI2 is the SPI2
    // (FSPI) peripheral on ESP32-S3.
    GPSPI2.clock.val = spiFrequencyToClockDiv(freq_hz);
    PSRAM_UNLOCK();
    return 0;
}

// ======================================================================
#elif defined(BOARD_HAS_NATIVE_PSRAM)  // Native memory-mapped PSRAM (ESP32-S3)
// ======================================================================

#include "esp_heap_caps.h"

// Tracking table — records our allocations so psram_free_all() works
typedef struct {
    void   *ptr;
    size_t  size;
} psram_native_alloc_t;

static psram_native_alloc_t psram_allocs[PSRAM_ALLOC_ENTRIES];
static int  psram_alloc_num = 0;
static bool psram_ok = false;

// ---- Read/write — direct memory access ----

void psram_read(uint32_t addr, uint8_t *buf, size_t len) {
    memcpy(buf, (const void *)addr, len);
}

void psram_write(uint32_t addr, const uint8_t *buf, size_t len) {
    memcpy((void *)addr, buf, len);
}

uint8_t  psram_read8(uint32_t addr)  { return *(const uint8_t *)addr; }
uint16_t psram_read16(uint32_t addr) { return *(const uint16_t *)addr; }
uint32_t psram_read32(uint32_t addr) { return *(const uint32_t *)addr; }
uint64_t psram_read64(uint32_t addr) { return *(const uint64_t *)addr; }

void psram_write8(uint32_t addr, uint8_t val)  { *(uint8_t *)addr = val; }
void psram_write16(uint32_t addr, uint16_t val) { *(uint16_t *)addr = val; }
void psram_write32(uint32_t addr, uint32_t val) { *(uint32_t *)addr = val; }
void psram_write64(uint32_t addr, uint64_t val) { *(uint64_t *)addr = val; }

// ---- Allocator — wraps ps_malloc / free ----

uint32_t psram_malloc(size_t size) {
    if (size == 0)
        return 0;

    PSRAM_LOCK();

    if (psram_ok && psram_alloc_num < PSRAM_ALLOC_ENTRIES) {
        void *p = ps_malloc(size);
        if (p) {
            psram_allocs[psram_alloc_num].ptr  = p;
            psram_allocs[psram_alloc_num].size = size;
            psram_alloc_num++;
            PSRAM_UNLOCK();
            return (uint32_t)p;
        }
    }

    // Fallback to system malloc (PSRAM full, out of entries, or not available)
    uint32_t result = psram_fb_malloc(size);
    PSRAM_UNLOCK();
    return result;
}

void psram_free(uint32_t addr) {
    if (addr == 0)
        return;

    PSRAM_LOCK();

    // Check PSRAM tracking table first
    for (int i = 0; i < psram_alloc_num; i++) {
        if ((uint32_t)psram_allocs[i].ptr == addr) {
            free(psram_allocs[i].ptr);
            // Compact: swap with last entry
            psram_allocs[i] = psram_allocs[psram_alloc_num - 1];
            psram_alloc_num--;
            PSRAM_UNLOCK();
            return;
        }
    }

    // Not in PSRAM table — try fallback table, then free directly
    if (!psram_fb_free(addr))
        free((void *)addr);

    PSRAM_UNLOCK();
}

void psram_free_all(void) {
    PSRAM_LOCK();
    for (int i = 0; i < psram_alloc_num; i++)
        free(psram_allocs[i].ptr);
    psram_alloc_num = 0;
    psram_fb_free_all();
    PSRAM_UNLOCK();
}

size_t psram_bytes_used(void) {
    PSRAM_LOCK();
    size_t total = 0;
    for (int i = 0; i < psram_alloc_num; i++)
        total += psram_allocs[i].size;
    PSRAM_UNLOCK();
    return total;
}

size_t psram_bytes_free(void) {
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

size_t psram_bytes_contiguous(void) {
    return heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
}

int psram_alloc_count(void) { return psram_alloc_num; }
int psram_alloc_entries_max(void) { return PSRAM_ALLOC_ENTRIES; }

// ---- Setup ----

int psram_setup(void) {
    psram_mutex_init();

    Serial.print("Init PSRAM... ");

    if (!psramFound()) {
        Serial.println("not detected");
        return -1;
    }

    psram_alloc_num = 0;
    psram_ok = true;
    Serial.printf("OK (%u KB native)\n", (unsigned)(esp_spiram_get_size() / 1024));
    return 0;
}

// ---- Memory test ----

int psram_test(bool forever) {
    if (!psram_ok) {
        printfnl(SOURCE_COMMANDS, "PSRAM not initialized\n");
        return -1;
    }
    if (psram_bytes_used() > 0 || psram_fb_num > 0) {
        printfnl(SOURCE_COMMANDS, "PSRAM test blocked — allocations exist\n");
        return -1;
    }

    // Allocate the largest available block and test it
    size_t avail = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (avail == 0) {
        printfnl(SOURCE_COMMANDS, "PSRAM test: no free memory\n");
        return -1;
    }

    uint8_t *buf = (uint8_t *)ps_malloc(avail);
    if (!buf) {
        printfnl(SOURCE_COMMANDS, "PSRAM test: allocation failed\n");
        return -1;
    }

    uint32_t pass = 0;

    if (forever) {
        printfnl(SOURCE_COMMANDS, "PSRAM test forever: %u bytes, press any key to stop\n", avail);
        while (Serial.available()) Serial.read();  // Drain stale input
    }

    int result = 0;
    do {
        uint32_t errors = 0;
        uint8_t pass_xor = (uint8_t)pass;

        printfnl(SOURCE_COMMANDS, "Pass %u: writing %u bytes...\n", pass + 1, avail);
        unsigned long t0 = micros();

        // Write pattern
        for (size_t i = 0; i < avail; i++) {
            buf[i] = (uint8_t)(i ^ (i >> 8) ^ pass_xor);
            if ((i & 0xFFFF) == 0) vTaskDelay(1);
        }

        unsigned long t_write = micros() - t0;

        printfnl(SOURCE_COMMANDS, "Pass %u: verifying...\n", pass + 1);
        unsigned long t1 = micros();

        // Verify
        for (size_t i = 0; i < avail; i++) {
            uint8_t expected = (uint8_t)(i ^ (i >> 8) ^ pass_xor);
            if (buf[i] != expected) {
                if (errors < 10)
                    printfnl(SOURCE_COMMANDS, "  MISMATCH at offset 0x%06X: wrote 0x%02X read 0x%02X\n",
                             i, expected, buf[i]);
                errors++;
            }
            if ((i & 0xFFFF) == 0) vTaskDelay(1);
        }

        unsigned long t_read = micros() - t1;

        // Benchmark results
        uint32_t write_kbps = (t_write > 0) ? (uint32_t)((uint64_t)avail * 1000 / t_write) : 0;
        uint32_t read_kbps  = (t_read  > 0) ? (uint32_t)((uint64_t)avail * 1000 / t_read)  : 0;
        printfnl(SOURCE_COMMANDS, "Pass %u: write %u KB/s, read %u KB/s\n",
                 pass + 1, write_kbps, read_kbps);

        if (errors > 0) {
            printfnl(SOURCE_COMMANDS, "PSRAM test FAILED on pass %u: %u errors\n", pass + 1, errors);
            result = -1;
            break;
        }

        printfnl(SOURCE_COMMANDS, "Pass %u: PASSED\n", pass + 1);
        pass++;

        // Check for user abort between passes
        if (forever && Serial.available()) {
            while (Serial.available()) Serial.read();
            printfnl(SOURCE_COMMANDS, "PSRAM test stopped by user after %u passes\n", pass);
            break;
        }

    } while (forever);

    free(buf);
    return result;
}

void     psram_cache_flush(void) {}
void     psram_cache_invalidate(void) {}
uint32_t psram_cache_hits(void)   { return 0; }
uint32_t psram_cache_misses(void) { return 0; }

void psram_print_map(void) {}           // No block table for native PSRAM
void psram_print_cache_map(void) {}
void psram_print_cache_detail(void) {}

uint32_t psram_size(void) { return esp_spiram_get_size(); }
bool psram_available(void) { return psram_ok; }
uint32_t psram_get_freq(void) { return 0; }
int psram_change_freq(uint32_t) { return -1; }

// ======================================================================
#else  // No PSRAM — stubs (all allocations go through system heap)
// ======================================================================

int psram_setup(void) { psram_mutex_init(); return 0; }
int psram_test(bool) { return 0; }
uint32_t psram_size(void) { return 0; }
bool     psram_available(void) { return false; }

uint8_t  psram_read8(uint32_t addr)  { return addr ? *(const uint8_t *)addr : 0; }
uint16_t psram_read16(uint32_t addr) { return addr ? *(const uint16_t *)addr : 0; }
uint32_t psram_read32(uint32_t addr) { return addr ? *(const uint32_t *)addr : 0; }
uint64_t psram_read64(uint32_t addr) { return addr ? *(const uint64_t *)addr : 0; }
void     psram_write8(uint32_t addr, uint8_t val)  { if (addr) *(uint8_t *)addr = val; }
void     psram_write16(uint32_t addr, uint16_t val) { if (addr) *(uint16_t *)addr = val; }
void     psram_write32(uint32_t addr, uint32_t val) { if (addr) *(uint32_t *)addr = val; }
void     psram_write64(uint32_t addr, uint64_t val) { if (addr) *(uint64_t *)addr = val; }
void     psram_read(uint32_t addr, uint8_t *buf, size_t len) { if (addr) memcpy(buf, (const void *)addr, len); }
void     psram_write(uint32_t addr, const uint8_t *buf, size_t len) { if (addr) memcpy((void *)addr, buf, len); }

uint32_t psram_malloc(size_t size) {
    if (size == 0) return 0;
    PSRAM_LOCK();
    uint32_t r = psram_fb_malloc(size);
    PSRAM_UNLOCK();
    return r;
}

void psram_free(uint32_t addr) {
    if (addr == 0) return;
    PSRAM_LOCK();
    if (!psram_fb_free(addr))
        free((void *)addr);
    PSRAM_UNLOCK();
}

void psram_free_all(void) {
    PSRAM_LOCK();
    psram_fb_free_all();
    PSRAM_UNLOCK();
}

size_t   psram_bytes_used(void) { return 0; }
size_t   psram_bytes_free(void) { return 0; }
size_t   psram_bytes_contiguous(void) { return 0; }
int      psram_alloc_count(void) { return 0; }
int      psram_alloc_entries_max(void) { return 0; }
void     psram_cache_flush(void) {}
void     psram_cache_invalidate(void) {}
uint32_t psram_cache_hits(void)   { return 0; }
uint32_t psram_cache_misses(void) { return 0; }
void     psram_print_map(void) {}
void     psram_print_cache_map(void) {}
void     psram_print_cache_detail(void) {}
uint32_t psram_get_freq(void) { return 0; }
int psram_change_freq(uint32_t) { return -1; }

#endif  // BOARD_HAS_IMPROVISED_PSRAM / BOARD_HAS_NATIVE_PSRAM / stubs

// ---- Memory operations (universal — work with any address type) ----

void psram_memset(uint32_t dst, uint8_t val, size_t len) {
    if (IS_ADDRESS_MAPPED(dst)) {
        memset((void *)dst, val, len);
        return;
    }
    uint8_t buf[64];
    memset(buf, val, sizeof(buf));
    while (len > 0) {
        size_t n = (len > sizeof(buf)) ? sizeof(buf) : len;
        psram_write(dst, buf, n);
        dst += n; len -= n;
    }
}

void psram_memcpy(uint32_t dst, uint32_t src, size_t len) {
    bool dm = IS_ADDRESS_MAPPED(dst);
    bool sm = IS_ADDRESS_MAPPED(src);

    if (dm && sm) {
        memcpy((void *)dst, (const void *)src, len);
    } else if (sm) {
        // Mapped src -> unmapped dst
        psram_write(dst, (const uint8_t *)src, len);
    } else if (dm) {
        // Unmapped src -> mapped dst
        psram_read(src, (uint8_t *)dst, len);
    } else {
        // Both unmapped — shuttle through a temp buffer
        uint8_t buf[64];
        while (len > 0) {
            size_t n = (len > sizeof(buf)) ? sizeof(buf) : len;
            psram_read(src, buf, n);
            psram_write(dst, buf, n);
            src += n; dst += n; len -= n;
        }
    }
}

int psram_memcmp(uint32_t addr1, uint32_t addr2, size_t len) {
    bool m1 = IS_ADDRESS_MAPPED(addr1);
    bool m2 = IS_ADDRESS_MAPPED(addr2);

    if (m1 && m2)
        return memcmp((const void *)addr1, (const void *)addr2, len);

    uint8_t buf1[64], buf2[64];
    while (len > 0) {
        size_t n = (len > sizeof(buf1)) ? sizeof(buf1) : len;

        if (m1) memcpy(buf1, (const void *)addr1, n);
        else    psram_read(addr1, buf1, n);

        if (m2) memcpy(buf2, (const void *)addr2, n);
        else    psram_read(addr2, buf2, n);

        int r = memcmp(buf1, buf2, n);
        if (r != 0) return r;

        addr1 += n; addr2 += n; len -= n;
    }
    return 0;
}
