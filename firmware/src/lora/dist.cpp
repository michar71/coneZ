#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include "mbedtls/md5.h"
#include "main.h"
#include "printManager.h"
#include "lora_proto.h"
#include "inflate.h"
#include "rs.h"
#include "dist.h"

// ===== ConeZ dist (Phase 3 core + Phase 4 compression + Phase 5 FEC) =========
// A file = total_blocks blocks of (uncompressed) block_size bytes. Each block is
// independently transferred as N DATA chunks (deflate-compressed when algo says
// so) plus R systematic-RS PARITY chunks (rs.c) computed ACROSS the chunks. We
// stage ONE block at a time; the block is recoverable as soon as we hold N of
// the N+R chunks (all data -> use directly; gaps -> RS erasure-recover the lost
// data from parity, same carousel cycle). The recovered compressed block is then
// inflated (or copied) into the whole-file buffer at its block offset and the
// block marked in a per-file bitmap; missed blocks are caught next cycle. When
// every block is present the file is MD5-verified and written to /dist/<name>.
// Firmware images exceed the RAM cap and are skipped here (OTA is Phase 6).

#define DIST_MAX_FILES   32
#define DIST_DIR_PATH    "/dist"
#define DIST_MAX_FILE    (256 * 1024)   // RAM-buffer cap (uncompressed)
#define DIST_L           LP_DIST_CHUNK_SIZE   // RS symbol size = chunk payload size

typedef struct {
    uint16_t id;
    char     name[40];
    uint32_t size;
    char     md5[9];        // 8 hex chars + NUL (build_manifest md5_8)
    uint8_t  algo;          // LP_DIST_ALGO_NONE / _DEFLATE
    uint32_t block_size;    // uncompressed block size
    uint16_t total_blocks;  // blocks in this file
    bool     wanted;        // local copy missing or hash mismatch
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
static uint8_t *rx_buf          = NULL;    // uncompressed whole-file buffer
static uint8_t *rx_block_bm     = NULL;    // total_blocks bits
static uint16_t rx_blocks_have  = 0;

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
            if (!strcmp(dfiles[k].name, e->d_name)) { in = true; break; }
        if (!in) {
            static char logical[300]; snprintf(logical, sizeof(logical), "%s/%s", DIST_DIR_PATH, e->d_name);
            static char path[320]; remove(lfs_path(path, sizeof(path), logical));
            printfnl(SOURCE_LORA, "dist: deleted %s (not in manifest)\n", e->d_name);
        }
    }
    closedir(dir);
}

// Parse the reassembled manifest text -> dfiles[]; mark wanted; reconcile deletes.
// [files] line: id \t name \t size \t md5 [ \t algo \t block_size \t total_blocks ]
static void manifest_parse(char *text, uint32_t len)
{
    (void)len;
    dfile_n = 0;
    char *save_line;
    char *line = strtok_r(text, "\n", &save_line);
    int section = 0;  // 1 = firmware (ignored here), 2 = files
    while (line && dfile_n < DIST_MAX_FILES) {
        if      (!strcmp(line, "[firmware]")) section = 1;
        else if (!strcmp(line, "[files]"))    section = 2;
        else if (line[0] && line[0] != '#' && section == 2) {
            char *save_tok;
            char *ids   = strtok_r(line, "\t", &save_tok);
            char *name  = ids   ? strtok_r(NULL, "\t",     &save_tok) : NULL;
            char *szs   = name  ? strtok_r(NULL, "\t",     &save_tok) : NULL;
            char *md5   = szs   ? strtok_r(NULL, "\t\r\n", &save_tok) : NULL;
            char *algos = md5   ? strtok_r(NULL, "\t\r\n", &save_tok) : NULL;
            char *bsz   = algos ? strtok_r(NULL, "\t\r\n", &save_tok) : NULL;
            char *tbs   = bsz   ? strtok_r(NULL, "\t\r\n", &save_tok) : NULL;
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
                d->wanted = false;
            }
        }
        line = strtok_r(NULL, "\n", &save_line);
    }

    for (int k = 0; k < dfile_n; k++) {
        char logical[128]; snprintf(logical, sizeof(logical), "%s/%s", DIST_DIR_PATH, dfiles[k].name);
        char have[9];
        if (!file_md5_8(logical, have) || strcmp(have, dfiles[k].md5) != 0)
            dfiles[k].wanted = true;
    }
    reconcile_deletes();
    have_manifest = true;
    cur_serial = rx_serial;
    int want = 0; for (int k = 0; k < dfile_n; k++) if (dfiles[k].wanted) want++;
    printfnl(SOURCE_LORA, "dist: manifest serial %u: %d files, %d to fetch\n",
             (unsigned)cur_serial, dfile_n, want);
}

static void complete(void)
{
    if (rx_file_id == LP_DIST_MANIFEST_ID) {
        manifest_parse((char *)rx_buf, rx_file_len);
    } else {
        dist_file_t *df = find_file(rx_file_id);
        if (df) {
            char got[9]; md5_8_buf(rx_buf, rx_file_len, got);
            if (!strcmp(got, df->md5)) {
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
                     uint8_t algo, uint32_t block_size)
{
    rx_reset();
    if (flen > DIST_MAX_FILE) {   // firmware-sized -> Phase 6, skip here
        printfnl(SOURCE_LORA, "dist: id %u too big to RAM-buffer (%u B), skipping\n",
                 (unsigned)fid, (unsigned)flen);
        return;
    }
    if (total_blocks == 0) total_blocks = 1;
    if (block_size == 0)   block_size = LP_DIST_BLOCK_SIZE;
    rx_file_id = fid; rx_serial = serial; rx_file_len = flen;
    rx_total_blocks = total_blocks; rx_algo = algo; rx_block_size = block_size;
    rx_blocks_have = 0;
    rx_buf      = (uint8_t *)malloc(flen ? flen : 1);
    rx_block_bm = (uint8_t *)calloc((total_blocks >> 3) + 1, 1);
    if (!rx_buf || !rx_block_bm) { printfnl(SOURCE_LORA, "dist: alloc fail id %u\n", (unsigned)fid); rx_reset(); }
}

static void start_block(uint16_t bidx, uint16_t N, uint16_t R, uint32_t comp_len)
{
    blk_reset();
    if (N == 0) return;
    blk_data      = (uint8_t *)calloc((size_t)N * DIST_L, 1);   // zero-pad for RS
    blk_data_have = (uint8_t *)calloc(N, 1);
    blk_par       = R ? (uint8_t *)calloc((size_t)R * DIST_L, 1) : NULL;
    blk_par_have  = R ? (uint8_t *)calloc(R, 1) : NULL;
    if (!blk_data || !blk_data_have || (R && (!blk_par || !blk_par_have))) { blk_reset(); return; }
    blk_idx = bidx; blk_N = N; blk_R = R; blk_comp_len = comp_len;
    blk_data_cnt = 0; blk_par_cnt = 0;
}

// The staged block holds enough chunks: (RS-recover if needed ->) inflate/copy
// into rx_buf at the block offset, mark it done, finalize the file when complete.
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
    uint8_t *dest  = rx_buf + off_u;

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

void dist_handle_chunk(const uint8_t *pkt, size_t len)
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
            start_rx(LP_DIST_MANIFEST_ID, serial, flen, tb, LP_DIST_ALGO_NONE, LP_DIST_BLOCK_SIZE);
        if (rx_file_id != LP_DIST_MANIFEST_ID) return;       // alloc failed / too big
    } else {
        // data/parity chunk: need the matching manifest, and the file must be wanted
        if (!have_manifest || serial != cur_serial) return;
        dist_file_t *df = find_file(fid);
        if (!df || !df->wanted) return;
        if (rx_file_id != fid) {
            if (rx_file_id != 0xFFFF) return;                // busy with another reassembly
            uint16_t fblocks = df->total_blocks ? df->total_blocks : tb;
            start_rx(fid, serial, flen, fblocks, df->algo, df->block_size);
            if (rx_file_id != fid) return;                   // alloc failed / too big
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

void dist_print_status(void)
{
    if (!have_manifest) { printfnl(SOURCE_COMMANDS, "dist: no manifest heard yet\n"); return; }
    printfnl(SOURCE_COMMANDS,
             "dist: manifest serial %u  %d files  %u chunks rx (%u parity)  %u RS-recovered  %u done\n",
             (unsigned)cur_serial, dfile_n, (unsigned)dist_chunks_rx, (unsigned)dist_par_rx,
             (unsigned)dist_rs_recover, (unsigned)dist_files_done);
    for (int k = 0; k < dfile_n; k++)
        printfnl(SOURCE_COMMANDS, "  [%u] %-22s %6u B  %s  %s/%ublk  %s\n",
                 (unsigned)dfiles[k].id, dfiles[k].name, (unsigned)dfiles[k].size,
                 dfiles[k].md5,
                 dfiles[k].algo == LP_DIST_ALGO_DEFLATE ? "deflate" : "store",
                 (unsigned)dfiles[k].total_blocks,
                 dfiles[k].wanted ? "WANTED" : "ok");
}
