#ifndef _conez_psram_h
#define _conez_psram_h

// ============================================================================
// ConeZ PSRAM Subsystem
// ============================================================================
//
// Unified memory API that works across all board configurations:
//
//   BOARD_HAS_IMPROVISED_PSRAM  (ConeZ PCB v0.1)
//     External LY68L6400SLIT 8MB SPI PSRAM on GPIO 4/5/6/7 (FSPI bus).
//     Accessed via SPI commands — not memory-mapped by the CPU. Addresses
//     returned by psram_malloc() are virtual (offset 0x10000000) and must
//     be read/written through psram_read()/psram_write() or the typed
//     accessors. A write-back DRAM page cache accelerates repeated access.
//
//   BOARD_HAS_NATIVE_PSRAM  (future boards)
//     ESP-IDF memory-mapped PSRAM. Addresses are real pointers and can be
//     dereferenced directly. The allocator wraps ps_malloc()/free().
//
//   Neither defined  (Heltec LoRa32 V3, or any board without PSRAM)
//     All allocations silently fall back to the system heap (malloc/free).
//     Every psram_* function still works — read/write dereference the
//     pointer, malloc/free use the heap. This lets callers use the PSRAM
//     API unconditionally without #ifdefs.
//
// Thread safety:
//   All public functions are protected by a recursive FreeRTOS mutex.
//   Safe to call from any task. The memory test (psram_test) runs without
//   the mutex and requires exclusive access — it will refuse to run if
//   any allocations exist.
//
// ============================================================================

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- Allocation table size ----
//
// The allocator uses a fixed-size table in internal SRAM (no dynamic memory).
// Each entry is 12 bytes. This sets the maximum number of simultaneous
// allocations. If the table is full, psram_malloc() falls back to the
// system heap. A separate fallback tracking table (same size) tracks those
// heap allocations so psram_free_all() can release them.
#ifndef PSRAM_ALLOC_ENTRIES
#define PSRAM_ALLOC_ENTRIES  128
#endif

// ---- Address classification ----
//
// IS_ADDRESS_MAPPED(addr)
//   Returns true if the address can be dereferenced directly by the CPU.
//   This includes internal SRAM, native (memory-mapped) PSRAM, flash cache,
//   and heap allocations — all of which live at 0x3C000000 and above on
//   ESP32-S3.
//
//   Returns false for improvised SPI-PSRAM virtual addresses in the range
//   0x10000000–0x107FFFFF. These addresses are offsets into the external
//   SPI PSRAM chip and must be accessed through psram_read()/psram_write().
//
//   Used internally by psram_read/write, psram_free, and psram_mem* to
//   choose the fast path (memcpy) vs the SPI path. Callers can also use
//   it to decide whether a psram_malloc() result can be used as a pointer.
#define IS_ADDRESS_MAPPED(addr)  ((uint32_t)(addr) >= 0x3C000000UL)

// ---- Setup and diagnostics ----

// Initialize the PSRAM hardware and allocator. Call once during setup().
// On improvised PSRAM: resets the SPI chip, verifies manufacturer ID, runs a
// quick write/read sanity check. On native PSRAM: checks psramFound().
// On boards without PSRAM: initializes the mutex only (always succeeds).
// Returns 0 on success, negative on failure. Failure is non-fatal — all
// psram_* functions gracefully fall back to the system heap.
int      psram_setup(void);

// Full memory test with optional continuous looping. Writes address-derived
// patterns across the entire PSRAM, reads back and verifies, and prints
// write/read throughput in KB/s. Pattern varies each pass to catch stuck bits.
// Refuses to run if any allocations exist (would corrupt live data).
// In forever mode, loops until an error occurs or the user presses a key.
// Returns 0 on pass, -1 on failure or if blocked.
int      psram_test(bool forever = false);

// Total PSRAM capacity in bytes (8388608 for improvised, esp_spiram_get_size()
// for native, 0 for no-PSRAM boards).
uint32_t psram_size(void);

// True if psram_setup() succeeded and hardware PSRAM is usable.
// When false, psram_malloc() still works — it uses the system heap.
bool     psram_available(void);

// Current SPI clock frequency in Hz (0 on boards without SPI PSRAM).
uint32_t psram_get_freq(void);

// Change SPI clock at runtime (1–80 MHz). Flushes cache, acquires mutex.
// Returns 0 on success, -1 on error. No-op on native/stub boards.
int      psram_change_freq(uint32_t freq_hz);

// ---- Read / Write ----
//
// All read/write functions accept addresses returned by psram_malloc().
// They transparently handle both address types:
//   - Mapped addresses (>= 0x3C000000): direct memcpy (fast path)
//   - SPI-PSRAM virtual addresses (0x10xxxxxx): SPI transfer via cache
//
// On improvised PSRAM, bulk transfers are chunked to respect the chip's
// 8us tCEM (CE# active time) limit, and routed through the DRAM page cache
// for write-back efficiency. Bounds-checked against BOARD_PSRAM_SIZE.
//
// The typed accessors (read8/16/32/64, write8/16/32/64) are convenience
// wrappers around psram_read/psram_write. Values are little-endian (native
// ESP32-S3 byte order). On no-PSRAM stub builds, null addresses return 0.

uint8_t  psram_read8(uint32_t addr);
uint16_t psram_read16(uint32_t addr);
uint32_t psram_read32(uint32_t addr);
uint64_t psram_read64(uint32_t addr);
void     psram_write8(uint32_t addr, uint8_t val);
void     psram_write16(uint32_t addr, uint16_t val);
void     psram_write32(uint32_t addr, uint32_t val);
void     psram_write64(uint32_t addr, uint64_t val);

void     psram_read(uint32_t addr, uint8_t *buf, size_t len);
void     psram_write(uint32_t addr, const uint8_t *buf, size_t len);

// ---- Allocator ----
//
// psram_malloc() returns a uint32_t address, not a pointer. On improvised
// PSRAM this is a virtual address (0x10xxxxxx) that cannot be dereferenced —
// use psram_read/write to access the data. On native PSRAM and stub builds,
// the returned address is a real pointer (castable to void*).
//
// Use IS_ADDRESS_MAPPED() to check whether the result can be used as a pointer.
//
// Returns 0 on failure (like standard malloc returning NULL). All allocations
// are 4-byte aligned.
//
// The allocator uses a fixed-size block table (PSRAM_ALLOC_ENTRIES slots).
// Each allocation or free-space region consumes one slot. When the table is
// full or PSRAM is unavailable, psram_malloc() falls back to the system heap
// transparently. These fallback allocations are tracked in a separate table
// so psram_free() and psram_free_all() can release them.
//
// psram_free(0) is a safe no-op. psram_free() on a mapped address checks
// the fallback table first, then calls free() directly as a last resort.
//
// psram_free_all() releases everything — PSRAM allocations, cache contents,
// and fallback heap allocations. Intended for cleanup between script runs.

uint32_t psram_malloc(size_t size);
void     psram_free(uint32_t addr);
void     psram_free_all(void);
size_t   psram_bytes_used(void);        // Total bytes in active PSRAM allocations
size_t   psram_bytes_free(void);        // Total free bytes in PSRAM
size_t   psram_bytes_contiguous(void);  // Largest single allocatable block
int      psram_alloc_count(void);       // Number of active PSRAM allocations
int      psram_alloc_entries_max(void); // Max allocation slots (PSRAM_ALLOC_ENTRIES)

// ---- DRAM page cache ----
//
// Write-back cache for improvised SPI PSRAM. Keeps recently-accessed pages
// in internal DRAM for fast repeated access. LRU eviction. Dirty pages are
// written back on eviction or explicit flush.
//
// Total DRAM cost: PSRAM_CACHE_PAGES * (PSRAM_CACHE_PAGE_SIZE + 12) bytes.
// Default: 64 * 512 = 32 KB.
//
// No-op on native PSRAM (already memory-mapped) and stub builds.
// Set PSRAM_CACHE_PAGES to 0 at compile time to disable.

#ifndef PSRAM_CACHE_PAGES
#define PSRAM_CACHE_PAGES       64  // Number of cached pages (0 = disabled)
#endif
#ifndef PSRAM_CACHE_PAGE_SIZE
#define PSRAM_CACHE_PAGE_SIZE  512  // Bytes per page (must be power of 2)
#endif

void     psram_cache_flush(void);       // Write all dirty pages back to PSRAM
void     psram_cache_invalidate(void);  // Discard all cached pages (drops dirty data!)
uint32_t psram_cache_hits(void);
uint32_t psram_cache_misses(void);

// ---- Memory operations ----
//
// Accept any address type (PSRAM virtual or mapped RAM). Use IS_ADDRESS_MAPPED()
// internally to pick memset/memcpy/memcmp for mapped addresses, or shuttle
// through a temporary buffer for SPI-PSRAM addresses. Both source and
// destination can be either type in psram_memcpy/psram_memcmp.

void psram_memset(uint32_t dst, uint8_t val, size_t len);
void psram_memcpy(uint32_t dst, uint32_t src, size_t len);
int  psram_memcmp(uint32_t addr1, uint32_t addr2, size_t len);

// ---- Visual memory map ----
//
// Prints a text-art bar showing allocation status across the PSRAM address
// space. Each character represents an equal slice of the total capacity.
// '-' = entirely free, '+' = partially allocated, '*' = fully allocated.
// No-op on boards without PSRAM or with native (memory-mapped) PSRAM
// where the internal block layout is not tracked.
void psram_print_map(void);
void psram_print_cache_map(void);
void psram_print_cache_detail(void);

#ifdef __cplusplus
}
#endif

#endif
