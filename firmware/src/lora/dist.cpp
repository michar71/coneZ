#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "mbedtls/md5.h"
#include "main.h"
#include "config.h"
#include "printManager.h"
#include "lora_proto.h"
#include "inflate.h"
#include "rs.h"
#include "ota_lock.h"
#include "dist.h"

// ===== ConeZ dist (Phases 3-6: core, deflate, RS FEC, OTA flash-stream) ======
// A file = total_blocks blocks of (uncompressed) block_size bytes. Each block is
// transferred as N DATA chunks (deflate-compressed when algo says so) + R
// systematic-RS PARITY chunks (rs.c) computed ACROSS chunks. We stage ONE block
// at a time; it is recoverable once N of the N+R chunks are held (all data ->
// use directly; gaps -> RS erasure-recover the same cycle). The recovered
// compressed block is inflated and committed:
//   * data file (kind=FILE): into a whole-file RAM buffer, MD5-verified, written
//     to /dist/<name>.
//   * firmware (kind=FW, Phase 6): streamed to the INACTIVE OTA partition at the
//     block offset (no whole-file RAM buffer); when all blocks are present,
//     esp_ota_set_boot_partition validates the image, then we reboot. Downgrades
//     are refused unless [lora] ota_downgrade is set (version is etched in-image).

#define DIST_MAX_FILES   32
#define DIST_DIR_PATH    "/dist"
#define DIST_MAX_FILE    (256 * 1024)   // RAM-buffer cap for data files (uncompressed)
#define DIST_L           LP_DIST_CHUNK_SIZE   // RS symbol size = chunk payload size

#define DIST_KIND_FILE   0
#define DIST_KIND_FW     1

// Sanity caps on manifest/chunk-supplied sizes -- the amateur-radio link is
// unauthenticated, so a garbled or hostile header must not drive a wild malloc.
#define DIST_MAX_BLOCK   (64 * 1024)   // max uncompressed block_size (fw_block malloc)
#define DIST_MAX_CHUNKS  255           // max N or R per block (RS is GF(256))
// Free an in-progress transfer if no dist traffic at all arrives for this long
// (the master beacons + rebroadcasts the manifest every ~10 s, so prolonged
// silence means it is gone; brief gaps still resume from the held buffers).
#define DIST_STALL_MS    (5 * 60 * 1000)

#ifndef SPI_FLASH_SEC_SIZE
#define SPI_FLASH_SEC_SIZE 4096
#endif

typedef struct {
    uint16_t id;
    char     name[40];      // data file: filename; firmware: version string
    uint32_t size;
    char     md5[9];        // 8 hex chars + NUL (build_manifest md5_8) -- data files
    uint8_t  algo;          // LP_DIST_ALGO_NONE / _DEFLATE
    uint32_t block_size;    // uncompressed block size
    uint16_t total_blocks;  // blocks in this file
    uint8_t  kind;          // DIST_KIND_FILE / _FW
    bool     wanted;        // local copy missing / hash differs (file); version newer (fw)
} dist_file_t;

static dist_file_t dfiles[DIST_MAX_FILES];
static int      dfile_n       = 0;
static uint16_t cur_serial    = 0;
static bool     have_manifest = false;

// ---- single-file reassembly context ----------------------------------------
static uint16_t rx_file_id      = 0xFFFF;  // 0xFFFF = idle; 0 = manifest
static uint16_t rx_serial       = 0;
static uint32_t rx_file_len     = 0;       // total uncompressed file length
static uint16_t rx_total_blocks = 0;
static uint32_t rx_block_size   = 0;       // uncompressed block size
static uint8_t  rx_algo         = LP_DIST_ALGO_NONE;
static uint8_t *rx_buf          = NULL;    // uncompressed whole-file buffer (data files)
static uint8_t *rx_block_bm     = NULL;    // total_blocks bits
static uint16_t rx_blocks_have  = 0;

// ---- firmware OTA streaming (Phase 6) --------------------------------------
static bool                   rx_is_fw = false;
static const esp_partition_t *fw_part  = NULL;   // inactive OTA partition
static uint8_t               *fw_block = NULL;   // decompressed block (block_size)
static bool                   dist_holds_ota = false;  // we hold the shared ota_lock

// Serializes dist_handle_chunk (LoRa task) against dist_abort (any task, e.g. the
// shell on `lora off`) so a teardown never frees buffers a commit is still using.
// Recursive + lazily created on the LoRa task (the only creator), so no race.
static SemaphoreHandle_t dist_mtx = NULL;
static uint32_t          last_chunk_ms = 0;      // uptime of the last dist packet
static inline void dist_lock(void)   { if (dist_mtx) xSemaphoreTakeRecursive(dist_mtx, portMAX_DELAY); }
static inline void dist_unlock(void) { if (dist_mtx) xSemaphoreGiveRecursive(dist_mtx); }

// ---- current staged block (data + parity chunks) ---------------------------
static uint16_t blk_idx       = 0xFFFF;    // 0xFFFF = none staged
static uint16_t blk_N         = 0;         // data chunks
static uint16_t blk_R         = 0;         // parity chunks
static uint32_t blk_comp_len  = 0;         // true compressed length of the block
static uint8_t *blk_data      = NULL;      // N*L (zero-padded)
static uint8_t *blk_par       = NULL;      // R*L
static uint8_t *blk_data_have = NULL;      // N flags
static uint8_t *blk_par_have  = NULL;      // R flags
static uint16_t blk_data_cnt  = 0;
static uint16_t blk_par_cnt   = 0;

static uint32_t dist_chunks_rx   = 0;
static uint32_t dist_par_rx      = 0;
static uint32_t dist_rs_recover  = 0;      // blocks reconstructed via RS
static uint32_t dist_files_done  = 0;

static inline void bset(uint8_t *b, int i) { b[i >> 3] |= (uint8_t)(1 << (i & 7)); }
static inline bool bget(uint8_t *b, int i) { return b[i >> 3] & (1 << (i & 7)); }

// Compare dotted-numeric versions ("0.02.0451"): <0 if a<b, 0 if equal, >0 if a>b.
static int version_cmp(const char *a, const char *b)
{
    while (*a || *b) {
        char *ea, *eb;
        long na = strtol(a, &ea, 10);
        long nb = strtol(b, &eb, 10);
        if (na != nb) return na < nb ? -1 : 1;
        a = (*ea == '.') ? ea + 1 : ea;
        b = (*eb == '.') ? eb + 1 : eb;
        if (!*a && !*b) break;
    }
    return 0;
}

static void blk_reset(void)
{
    free(blk_data);      blk_data = NULL;
    free(blk_par);       blk_par = NULL;
    free(blk_data_have); blk_data_have = NULL;
    free(blk_par_have);  blk_par_have = NULL;
    blk_idx = 0xFFFF; blk_N = 0; blk_R = 0; blk_comp_len = 0;
    blk_data_cnt = 0; blk_par_cnt = 0;
}

static void rx_reset(void)
{
    blk_reset();
    free(rx_buf);      rx_buf = NULL;
    free(rx_block_bm); rx_block_bm = NULL;
    free(fw_block);    fw_block = NULL;   // partial flash writes are harmless (boot not set)
    if (dist_holds_ota) { ota_lock_release(); dist_holds_ota = false; }
    rx_is_fw = false; fw_part = NULL;
    rx_file_id = 0xFFFF; rx_total_blocks = 0; rx_blocks_have = 0;
    rx_file_len = 0; rx_block_size = 0; rx_algo = LP_DIST_ALGO_NONE;
}

static dist_file_t *find_file(uint16_t id)
{
    for (int i = 0; i < dfile_n; i++) if (dfiles[i].id == id) return &dfiles[i];
    return NULL;
}

static void md5_8_buf(const uint8_t *d, size_t n, char out[9])
{
    uint8_t dig[16];
    mbedtls_md5(d, n, dig);
    snprintf(out, 9, "%02x%02x%02x%02x", dig[0], dig[1], dig[2], dig[3]);
}

static bool file_md5_8(const char *logical, char out[9])
{
    char path[96];
    FILE *f = fopen(lfs_path(path, sizeof(path), logical), "rb");
    if (!f) return false;
    mbedtls_md5_context c; mbedtls_md5_init(&c); mbedtls_md5_starts(&c);
    uint8_t buf[256]; size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) mbedtls_md5_update(&c, buf, r);
    fclose(f);
    uint8_t dig[16]; mbedtls_md5_finish(&c, dig); mbedtls_md5_free(&c);
    snprintf(out, 9, "%02x%02x%02x%02x", dig[0], dig[1], dig[2], dig[3]);
    return true;
}

// Delete /dist/ files that are no longer in the manifest.
static void reconcile_deletes(void)
{
    char dirp[96];
    DIR *dir = opendir(lfs_path(dirp, sizeof(dirp), DIST_DIR_PATH));
    if (!dir) return;
    struct dirent *e;
    while ((e = readdir(dir))) {
        if (e->d_name[0] == '.') continue;
        bool in = false;
        for (int k = 0; k < dfile_n; k++)
            if (dfiles[k].kind == DIST_KIND_FILE && !strcmp(dfiles[k].name, e->d_name)) { in = true; break; }
        if (!in) {
            static char logical[300]; snprintf(logical, sizeof(logical), "%s/%s", DIST_DIR_PATH, e->d_name);
            static char path[320]; remove(lfs_path(path, sizeof(path), logical));
            printfnl(SOURCE_LORA, "dist: deleted %s (not in manifest)\n", e->d_name);
        }
    }
    closedir(dir);
}

// Parse the reassembled manifest text -> dfiles[]; mark wanted; reconcile deletes.
//   [firmware] line: id \t product \t version \t size \t md5 [ \t algo \t bsz \t tb ]
//   [files]    line: id \t name    \t size    \t md5  [ \t algo \t bsz \t tb ]
static void manifest_parse(char *text, uint32_t len)
{
    (void)len;
    const esp_app_desc_t *self = esp_app_get_description();
    bool have_fw = false;
    dfile_n = 0;
    char *save_line;
    char *line = strtok_r(text, "\n", &save_line);
    int section = 0;  // 1 = firmware, 2 = files
    while (line && dfile_n < DIST_MAX_FILES) {
        if      (!strcmp(line, "[firmware]")) section = 1;
        else if (!strcmp(line, "[files]"))    section = 2;
        else if (line[0] && line[0] != '#' && section == 1) {
            char *st;
            char *ids  = strtok_r(line, "\t", &st);
            char *prod = ids  ? strtok_r(NULL, "\t",     &st) : NULL;
            char *ver  = prod ? strtok_r(NULL, "\t",     &st) : NULL;
            char *szs  = ver  ? strtok_r(NULL, "\t",     &st) : NULL;
            char *md5  = szs  ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *alg  = md5  ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *bsz  = alg  ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *tbs  = bsz  ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            // Only OUR product, with full block geometry, and just one fw entry.
            if (ids && prod && ver && szs && md5 && alg && bsz && tbs &&
                self && !strcmp(prod, self->project_name) && !have_fw) {
                have_fw = true;
                dist_file_t *d = &dfiles[dfile_n++];
                d->id   = (uint16_t)atoi(ids);
                strlcpy(d->name, ver, sizeof(d->name));      // version string
                d->size = (uint32_t)strtoul(szs, NULL, 10);
                strlcpy(d->md5, md5, sizeof(d->md5));
                d->algo = (uint8_t)atoi(alg);
                d->block_size = (uint32_t)strtoul(bsz, NULL, 10);
                if (d->block_size == 0) d->block_size = LP_DIST_BLOCK_SIZE;
                d->total_blocks = (uint16_t)atoi(tbs);
                if (d->total_blocks == 0) d->total_blocks = 1;
                d->kind = DIST_KIND_FW;
                int cmp = version_cmp(ver, self->version);    // >0 = manifest newer
                d->wanted = config.lora_ota_downgrade ? (cmp != 0) : (cmp > 0);
                if (d->wanted)
                    printfnl(SOURCE_LORA, "dist: firmware %s available (running %s)%s\n",
                             ver, self->version, cmp <= 0 ? " [downgrade]" : "");
            }
        }
        else if (line[0] && line[0] != '#' && section == 2) {
            char *st;
            char *ids   = strtok_r(line, "\t", &st);
            char *name  = ids   ? strtok_r(NULL, "\t",     &st) : NULL;
            char *szs   = name  ? strtok_r(NULL, "\t",     &st) : NULL;
            char *md5   = szs   ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *algos = md5   ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *bsz   = algos ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            char *tbs   = bsz   ? strtok_r(NULL, "\t\r\n", &st) : NULL;
            if (ids && name && szs && md5) {
                dist_file_t *d = &dfiles[dfile_n++];
                d->id   = (uint16_t)atoi(ids);
                strlcpy(d->name, name, sizeof(d->name));
                d->size = (uint32_t)strtoul(szs, NULL, 10);
                strlcpy(d->md5, md5, sizeof(d->md5));
                d->algo       = algos ? (uint8_t)atoi(algos) : LP_DIST_ALGO_NONE;
                d->block_size = bsz   ? (uint32_t)strtoul(bsz, NULL, 10) : LP_DIST_BLOCK_SIZE;
                if (d->block_size == 0) d->block_size = LP_DIST_BLOCK_SIZE;
                d->total_blocks = tbs ? (uint16_t)atoi(tbs)
                                      : (uint16_t)((d->size + d->block_size - 1) / d->block_size);
                if (d->total_blocks == 0) d->total_blocks = 1;
                d->kind = DIST_KIND_FILE;
                d->wanted = false;
            }
        }
        line = strtok_r(NULL, "\n", &save_line);
    }

    for (int k = 0; k < dfile_n; k++) {
        if (dfiles[k].kind != DIST_KIND_FILE) continue;   // fw wanted set above
        char logical[128]; snprintf(logical, sizeof(logical), "%s/%s", DIST_DIR_PATH, dfiles[k].name);
        char have[9];
        if (!file_md5_8(logical, have) || strcmp(have, dfiles[k].md5) != 0)
            dfiles[k].wanted = true;
    }
    reconcile_deletes();
    have_manifest = true;
    cur_serial = rx_serial;
    int want = 0; for (int k = 0; k < dfile_n; k++) if (dfiles[k].wanted) want++;
    printfnl(SOURCE_LORA, "dist: manifest serial %u: %d entries, %d to fetch\n",
             (unsigned)cur_serial, dfile_n, want);
}

static void complete(void)
{
    if (rx_is_fw) {
        dist_file_t *df = find_file(rx_file_id);
        esp_err_t e = esp_ota_set_boot_partition(fw_part);   // validates the image
        if (e == ESP_OK) {
            printfnl(SOURCE_LORA, "dist: firmware %s flashed + verified, rebooting...\n",
                     df ? df->name : "");
            vTaskDelay(pdMS_TO_TICKS(1500));                 // let the log drain
            esp_restart();
        }
        printfnl(SOURCE_LORA, "dist: firmware image verify failed (%s), will refetch\n",
                 esp_err_to_name(e));
        rx_reset();
        return;
    }

    if (rx_file_id == LP_DIST_MANIFEST_ID) {
        manifest_parse((char *)rx_buf, rx_file_len);
    } else {
        dist_file_t *df = find_file(rx_file_id);
        if (df) {
            char got[9]; md5_8_buf(rx_buf, rx_file_len, got);
            if (!strcmp(got, df->md5) && !littlefs_mounted) {
                // a concurrent HTTP filesystem upload has unmounted LittleFS --
                // don't fopen/fwrite into the gap; the carousel refetches next pass
                printfnl(SOURCE_LORA, "dist: %s verified but filesystem busy, retry next cycle\n", df->name);
            } else if (!strcmp(got, df->md5)) {
                char mk[96]; mkdir(lfs_path(mk, sizeof(mk), DIST_DIR_PATH), 0755);  // ensure /dist
                char logical[128]; snprintf(logical, sizeof(logical), "%s/%s", DIST_DIR_PATH, df->name);
                char path[160]; FILE *f = fopen(lfs_path(path, sizeof(path), logical), "wb");
                if (f) {
                    fwrite(rx_buf, 1, rx_file_len, f); fclose(f);
                    df->wanted = false; dist_files_done++;
                    printfnl(SOURCE_LORA, "dist: %s complete + verified (%s, %u B)\n",
                             df->name, df->md5, (unsigned)rx_file_len);
                } else {
                    printfnl(SOURCE_LORA, "dist: %s write failed\n", df->name);
                }
            } else {
                printfnl(SOURCE_LORA, "dist: %s hash mismatch (got %s want %s), retry next cycle\n",
                         df->name, got, df->md5);
            }
        }
    }
    rx_reset();
}

static void start_rx(uint16_t fid, uint16_t serial, uint32_t flen, uint16_t total_blocks,
                     uint8_t algo, uint32_t block_size, uint8_t kind)
{
    rx_reset();
    if (total_blocks == 0) total_blocks = 1;
    if (block_size == 0)   block_size = LP_DIST_BLOCK_SIZE;
    if (block_size > DIST_MAX_BLOCK) {              // hostile/garbled manifest guard
        printfnl(SOURCE_LORA, "dist: id %u block_size %u too large, skipping\n",
                 (unsigned)fid, (unsigned)block_size);
        return;                                     // stay idle (rx_file_id == 0xFFFF)
    }

    if (kind == DIST_KIND_FW) {
        // Erase + write the inactive OTA partition PER BLOCK (small flash windows,
        // like the HTTP OTA's per-sector granularity). A single whole-image erase
        // keeps the flash cache disabled for seconds and collides with concurrent
        // LittleFS access on the other core (Cache-disabled panic).
        fw_part = esp_ota_get_next_update_partition(NULL);
        if (!fw_part) { printfnl(SOURCE_LORA, "dist: no OTA partition\n"); rx_reset(); return; }
        // Refuse to start if an HTTP firmware/filesystem update owns the flash --
        // both target the same inactive slot; interleaving corrupts the image.
        if (!ota_lock_acquire("lora-fw-ota")) {
            printfnl(SOURCE_LORA, "dist: OTA busy (%s in progress), deferring firmware\n",
                     ota_lock_owner() ? ota_lock_owner() : "?");
            rx_reset(); return;
        }
        dist_holds_ota = true;
        fw_block    = (uint8_t *)malloc(block_size);
        rx_block_bm = (uint8_t *)calloc((total_blocks >> 3) + 1, 1);
        if (!fw_block || !rx_block_bm) { printfnl(SOURCE_LORA, "dist: OTA alloc fail\n"); rx_reset(); return; }
        rx_is_fw = true;
        rx_file_id = fid; rx_serial = serial; rx_file_len = flen;
        rx_total_blocks = total_blocks; rx_algo = algo; rx_block_size = block_size; rx_blocks_have = 0;
        return;
    }

    if (flen > DIST_MAX_FILE) {   // oversized data file (shouldn't happen) -> skip
        printfnl(SOURCE_LORA, "dist: id %u too big to RAM-buffer (%u B), skipping\n",
                 (unsigned)fid, (unsigned)flen);
        return;
    }
    rx_file_id = fid; rx_serial = serial; rx_file_len = flen;
    rx_total_blocks = total_blocks; rx_algo = algo; rx_block_size = block_size; rx_blocks_have = 0;
    rx_buf      = (uint8_t *)malloc(flen ? flen : 1);
    rx_block_bm = (uint8_t *)calloc((total_blocks >> 3) + 1, 1);
    if (!rx_buf || !rx_block_bm) { printfnl(SOURCE_LORA, "dist: alloc fail id %u\n", (unsigned)fid); rx_reset(); }
}

static void start_block(uint16_t bidx, uint16_t N, uint16_t R, uint32_t comp_len)
{
    blk_reset();
    if (N == 0 || N > DIST_MAX_CHUNKS || R > DIST_MAX_CHUNKS) return;  // RS / sanity bound
    blk_data      = (uint8_t *)calloc((size_t)N * DIST_L, 1);   // zero-pad for RS
    blk_data_have = (uint8_t *)calloc(N, 1);
    blk_par       = R ? (uint8_t *)calloc((size_t)R * DIST_L, 1) : NULL;
    blk_par_have  = R ? (uint8_t *)calloc(R, 1) : NULL;
    if (!blk_data || !blk_data_have || (R && (!blk_par || !blk_par_have))) { blk_reset(); return; }
    blk_idx = bidx; blk_N = N; blk_R = R; blk_comp_len = comp_len;
    blk_data_cnt = 0; blk_par_cnt = 0;
}

// The staged block holds enough chunks: (RS-recover if needed ->) inflate/copy
// and commit (RAM buffer for a data file; flash for firmware); mark it done,
// finalize the file when every block is present.
static void finalize_block(bool use_rs)
{
    if (use_rs) {
        uint16_t lost = blk_N - blk_data_cnt;
        if (rs_decode(blk_N, blk_R, DIST_L, blk_data, blk_data_have,
                      blk_par, blk_par_have) != 0) {
            blk_reset();   // couldn't recover (alloc/insufficient) -> refetch next cycle
            return;
        }
        dist_rs_recover++;
        printfnl(SOURCE_LORA, "dist: id %u block %u RS-recovered %u lost data from %u parity\n",
                 (unsigned)rx_file_id, (unsigned)blk_idx, (unsigned)lost, (unsigned)blk_par_cnt);
    }

    uint32_t off_u = (uint32_t)blk_idx * rx_block_size;
    uint32_t ulen  = (off_u >= rx_file_len) ? 0
                   : (rx_file_len - off_u < rx_block_size ? rx_file_len - off_u : rx_block_size);

    uint8_t *dest = rx_is_fw ? fw_block : (rx_buf + off_u);

    if (rx_algo == LP_DIST_ALGO_DEFLATE) {
        int n = inflate_buf(blk_data, blk_comp_len, dest, ulen);
        if (n != (int)ulen) {
            printfnl(SOURCE_LORA, "dist: id %u block %u inflate %d != %u, retry next cycle\n",
                     (unsigned)rx_file_id, (unsigned)blk_idx, n, (unsigned)ulen);
            blk_reset();
            return;
        }
    } else {
        if (blk_comp_len != ulen) {
            printfnl(SOURCE_LORA, "dist: id %u block %u len %u != %u, retry next cycle\n",
                     (unsigned)rx_file_id, (unsigned)blk_idx, (unsigned)blk_comp_len, (unsigned)ulen);
            blk_reset();
            return;
        }
        if (ulen) memcpy(dest, blk_data, ulen);
    }

    if (rx_is_fw) {
        // Commit ONE flash sector (4 KB) at a time, yielding between. With
        // CONFIG_SPI_FLASH_AUTO_SUSPEND the cache stays enabled during each
        // erase/program (no cross-core cache-disable stall), so this never blocks
        // the system; per-block timing is logged for OTA progress visibility.
        uint32_t t0 = uptime_ms(), maxop = 0;
        esp_err_t e = ESP_OK;
        for (uint32_t s = 0; s < ulen && e == ESP_OK; s += SPI_FLASH_SEC_SIZE) {
            uint32_t seg  = (ulen - s < SPI_FLASH_SEC_SIZE) ? (ulen - s) : SPI_FLASH_SEC_SIZE;
            uint32_t wseg = (seg + 3u) & ~3u;              // flash writes are 4-aligned
            if (wseg > seg) memset(fw_block + s + seg, 0, wseg - seg);
            uint32_t a = uptime_ms();
            e = esp_partition_erase_range(fw_part, off_u + s, SPI_FLASH_SEC_SIZE);
            if (e == ESP_OK) e = esp_partition_write(fw_part, off_u + s, fw_block + s, wseg);
            uint32_t d = uptime_ms() - a;
            if (d > maxop) maxop = d;
            vTaskDelay(1);                                 // let RX/idle breathe between sectors
        }
        if (e != ESP_OK) {
            printfnl(SOURCE_LORA, "dist: id %u block %u flash erase/write failed (%s), retry next cycle\n",
                     (unsigned)rx_file_id, (unsigned)blk_idx, esp_err_to_name(e));
            blk_reset();
            return;
        }
        printfnl(SOURCE_LORA, "dist: fw block %u/%u committed (%u ms, max op %u ms)\n",
                 (unsigned)(blk_idx + 1), (unsigned)rx_total_blocks,
                 (unsigned)(uptime_ms() - t0), (unsigned)maxop);
    }

    if (!bget(rx_block_bm, blk_idx)) { bset(rx_block_bm, blk_idx); rx_blocks_have++; }
    blk_reset();
    if (rx_blocks_have >= rx_total_blocks) complete();
}

static void try_finalize(void)
{
    if (blk_data_cnt >= blk_N)                      finalize_block(false);  // all data
    else if (blk_data_cnt + blk_par_cnt >= blk_N)   finalize_block(true);   // RS recover
}

static void add_data_chunk(uint16_t idx, const uint8_t *payload, size_t plen)
{
    if (!blk_data || idx >= blk_N || blk_data_have[idx]) return;
    if (plen > DIST_L) plen = DIST_L;
    memcpy(blk_data + (size_t)idx * DIST_L, payload, plen);   // tail stays zero (calloc)
    blk_data_have[idx] = 1; blk_data_cnt++;
    try_finalize();
}

static void add_par_chunk(uint16_t idx, const uint8_t *payload, size_t plen)
{
    if (!blk_par || idx >= blk_R || blk_par_have[idx]) return;
    if (plen > DIST_L) plen = DIST_L;
    memcpy(blk_par + (size_t)idx * DIST_L, payload, plen);
    blk_par_have[idx] = 1; blk_par_cnt++;
    try_finalize();
}

static void dist_handle_chunk_locked(const uint8_t *pkt, size_t len)
{
    if (len < LP_DIST_PAYLOAD) return;
    bool     is_par = (pkt[LP_HDR_TYPE] == LP_PKT_DIST_PARITY);
    uint16_t serial = lp_rd_u16(pkt + LP_DIST_SERIAL);
    uint16_t fid    = lp_rd_u16(pkt + LP_DIST_FILE_ID);
    uint32_t flen   = lp_rd_u32(pkt + LP_DIST_FILE_LEN);
    uint16_t bidx   = lp_rd_u16(pkt + LP_DIST_BLOCK_IDX);
    uint16_t tb     = lp_rd_u16(pkt + LP_DIST_TOTAL_BLOCKS);
    uint16_t cidx   = lp_rd_u16(pkt + LP_DIST_CHUNK_IDX);
    uint16_t N      = lp_rd_u16(pkt + LP_DIST_DATA_CHUNKS);
    uint16_t R      = lp_rd_u16(pkt + LP_DIST_PARITY_CHUNKS);
    uint32_t clen   = lp_rd_u32(pkt + LP_DIST_BLOCK_COMP_LEN);
    const uint8_t *payload = pkt + LP_DIST_PAYLOAD;
    size_t plen = len - LP_DIST_PAYLOAD;
    dist_chunks_rx++;
    if (is_par) dist_par_rx++;
    if (tb == 0 || N == 0) return;

    if (fid == LP_DIST_MANIFEST_ID) {
        if (have_manifest && serial == cur_serial) return;   // already have this manifest
        if (rx_file_id != LP_DIST_MANIFEST_ID || rx_serial != serial)
            start_rx(LP_DIST_MANIFEST_ID, serial, flen, tb, LP_DIST_ALGO_NONE, LP_DIST_BLOCK_SIZE, DIST_KIND_FILE);
        if (rx_file_id != LP_DIST_MANIFEST_ID) return;       // alloc failed / too big
    } else {
        // data/parity chunk: need the matching manifest, and the file must be wanted
        if (!have_manifest || serial != cur_serial) return;
        dist_file_t *df = find_file(fid);
        if (!df || !df->wanted) return;
        if (rx_file_id != fid) {
            if (rx_file_id != 0xFFFF) return;                // busy with another reassembly
            uint16_t fblocks = df->total_blocks ? df->total_blocks : tb;
            start_rx(fid, serial, flen, fblocks, df->algo, df->block_size, df->kind);
            if (rx_file_id != fid) return;                   // alloc failed / too big
            if (df->kind == DIST_KIND_FW)
                printfnl(SOURCE_LORA, "dist: fetching firmware %s (%u B, %u blocks, %s)\n",
                         df->name, (unsigned)flen, (unsigned)fblocks,
                         df->algo == LP_DIST_ALGO_DEFLATE ? "deflate" : "store");
            else
                printfnl(SOURCE_LORA, "dist: fetching %s (%u B, %u blocks, %s)\n",
                         df->name, (unsigned)flen, (unsigned)fblocks,
                         df->algo == LP_DIST_ALGO_DEFLATE ? "deflate" : "store");
        }
    }

    // reassemble this block within the current file
    if (bidx >= rx_total_blocks) return;
    if (bget(rx_block_bm, bidx)) return;            // block already committed
    if (blk_idx != bidx) start_block(bidx, N, R, clen);   // (re)stage; abandons any partial
    if (blk_idx != bidx) return;                    // start_block alloc failed
    if (blk_N != N || blk_R != R) return;           // inconsistent geometry, ignore
    if (is_par) add_par_chunk(cidx, payload, plen);
    else        add_data_chunk(cidx, payload, plen);
}

// Called from the LoRa task on every DIST_DATA/DIST_PARITY packet. Serializes the
// whole reassembly/commit against dist_abort() (which may run on another task).
void dist_handle_chunk(const uint8_t *pkt, size_t len)
{
    if (!dist_mtx) dist_mtx = xSemaphoreCreateRecursiveMutex();  // LoRa task: sole creator
    last_chunk_ms = uptime_ms();          // any dist packet means the master is present
    dist_lock();
    dist_handle_chunk_locked(pkt, len);
    dist_unlock();
}

// Free an in-progress transfer's buffers (and release the OTA lock). Safe to call
// from any task -- the dist mutex serializes it against a live commit. Used on
// `lora off` so a disabled radio doesn't strand 16 KB-256 KB indefinitely.
void dist_abort(void)
{
    if (!dist_mtx) return;                 // nothing ever started -> nothing to free
    dist_lock();
    if (rx_file_id != 0xFFFF) {
        printfnl(SOURCE_LORA, "dist: aborting in-progress transfer, freeing buffers\n");
        rx_reset();
    }
    dist_unlock();
}

// Periodic tick from the LoRa task: if a transfer has stalled (no dist traffic at
// all for DIST_STALL_MS -> master gone), free it rather than hold the buffers for
// the rest of the boot. Brief gaps keep the buffers and resume. Cheap when idle.
void dist_tick(void)
{
    if (rx_file_id == 0xFFFF) return;                          // no transfer in flight
    if ((uint32_t)(uptime_ms() - last_chunk_ms) < DIST_STALL_MS) return;
    dist_lock();
    if (rx_file_id != 0xFFFF &&
        (uint32_t)(uptime_ms() - last_chunk_ms) >= DIST_STALL_MS) {
        printfnl(SOURCE_LORA, "dist: transfer stalled %us (master lost?), freeing buffers\n",
                 (unsigned)(DIST_STALL_MS / 1000));
        rx_reset();
    }
    dist_unlock();
}

// Isolated OTA-partition flash stress: per-sector erase+write of `kb` KB to the
// inactive OTA partition, timing each op. Lets us reproduce/iterate the flash
// path from the CLI (no LoRa), to tell a flash-path watchdog from a concurrency
// one. `yield_ms` > 0 inserts a vTaskDelay between sectors.
void dist_ota_selftest(int kb, int yield_ms)
{
    const esp_partition_t *p = esp_ota_get_next_update_partition(NULL);
    if (!p) { printfnl(SOURCE_COMMANDS, "ota selftest: no OTA partition\n"); return; }
    uint8_t *buf = (uint8_t *)malloc(SPI_FLASH_SEC_SIZE);
    if (!buf) { printfnl(SOURCE_COMMANDS, "ota selftest: no mem\n"); return; }
    memset(buf, 0xA5, SPI_FLASH_SEC_SIZE);
    uint32_t total = (uint32_t)kb * 1024;
    if (total > p->size) total = p->size;
    uint32_t t0 = uptime_ms(), max_e = 0, max_w = 0;
    esp_err_t e = ESP_OK;
    printfnl(SOURCE_COMMANDS, "ota selftest: %u KB to '%s', yield=%d ms...\n",
             (unsigned)(total / 1024), p->label, yield_ms);
    for (uint32_t off = 0; off < total && e == ESP_OK; off += SPI_FLASH_SEC_SIZE) {
        uint32_t a = uptime_ms();
        e = esp_partition_erase_range(p, off, SPI_FLASH_SEC_SIZE);
        uint32_t b = uptime_ms();
        if (e == ESP_OK) e = esp_partition_write(p, off, buf, SPI_FLASH_SEC_SIZE);
        uint32_t c = uptime_ms();
        if (b - a > max_e) max_e = b - a;
        if (c - b > max_w) max_w = c - b;
        if (yield_ms > 0) vTaskDelay(pdMS_TO_TICKS(yield_ms));
    }
    printfnl(SOURCE_COMMANDS, "ota selftest: done %u KB in %u ms, max erase %u ms, max write %u ms, rc=%s\n",
             (unsigned)(total / 1024), (unsigned)(uptime_ms() - t0),
             (unsigned)max_e, (unsigned)max_w, esp_err_to_name(e));
    free(buf);
}

void dist_print_status(void)
{
    if (!have_manifest) { printfnl(SOURCE_COMMANDS, "dist: no manifest heard yet\n"); return; }
    printfnl(SOURCE_COMMANDS,
             "dist: manifest serial %u  %d entries  %u chunks rx (%u parity)  %u RS-recovered  %u done\n",
             (unsigned)cur_serial, dfile_n, (unsigned)dist_chunks_rx, (unsigned)dist_par_rx,
             (unsigned)dist_rs_recover, (unsigned)dist_files_done);
    for (int k = 0; k < dfile_n; k++)
        printfnl(SOURCE_COMMANDS, "  [%u]%s %-20s %7u B  %s/%ublk  %s\n",
                 (unsigned)dfiles[k].id, dfiles[k].kind == DIST_KIND_FW ? " FW" : "   ",
                 dfiles[k].name, (unsigned)dfiles[k].size,
                 dfiles[k].algo == LP_DIST_ALGO_DEFLATE ? "deflate" : "store",
                 (unsigned)dfiles[k].total_blocks,
                 dfiles[k].wanted ? "WANTED" : "ok");
}
