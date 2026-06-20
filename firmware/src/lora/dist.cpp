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
#include "dist.h"

// ===== ConeZ dist (Phase 3 core) =============================================
// One reassembly context at a time (manifest takes priority, then files one at
// a time). Each file is RAM-buffered by chunk offset, MD5-verified against the
// manifest, then written to /dist/<name>. Firmware images (Phase 6) exceed the
// RAM cap and are skipped here.

#define DIST_MAX_FILES   32
#define DIST_DIR_PATH    "/dist"
#define DIST_MAX_FILE    (256 * 1024)   // Phase 3 RAM-buffer cap

typedef struct {
    uint16_t id;
    char     name[40];
    uint32_t size;
    char     md5[9];      // 8 hex chars + NUL (build_manifest md5_8)
    bool     wanted;      // local copy missing or hash mismatch
} dist_file_t;

static dist_file_t dfiles[DIST_MAX_FILES];
static int      dfile_n       = 0;
static uint16_t cur_serial    = 0;
static bool     have_manifest = false;

// single reassembly context
static uint16_t rx_file_id  = 0xFFFF;   // 0xFFFF = idle; 0 = manifest
static uint16_t rx_serial   = 0;
static uint32_t rx_file_len  = 0;
static uint16_t rx_total    = 0;
static uint16_t rx_have     = 0;
static uint8_t *rx_buf      = NULL;
static uint8_t *rx_bitmap   = NULL;

static uint32_t dist_chunks_rx  = 0;
static uint32_t dist_files_done = 0;

static inline void bset(uint8_t *b, int i) { b[i >> 3] |= (uint8_t)(1 << (i & 7)); }
static inline bool bget(uint8_t *b, int i) { return b[i >> 3] & (1 << (i & 7)); }

static void rx_reset(void)
{
    free(rx_buf);    rx_buf = NULL;
    free(rx_bitmap); rx_bitmap = NULL;
    rx_file_id = 0xFFFF; rx_have = 0; rx_total = 0; rx_file_len = 0;
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
static void manifest_parse(char *text, uint32_t len)
{
    dfile_n = 0;
    char *save_line;
    char *line = strtok_r(text, "\n", &save_line);
    int section = 0;  // 1 = firmware (ignored in Phase 3), 2 = files
    while (line && dfile_n < DIST_MAX_FILES) {
        if      (!strcmp(line, "[firmware]")) section = 1;
        else if (!strcmp(line, "[files]"))    section = 2;
        else if (line[0] && line[0] != '#' && section == 2) {
            char *save_tok;
            char *ids  = strtok_r(line, "\t", &save_tok);
            char *name = ids  ? strtok_r(NULL, "\t",     &save_tok) : NULL;
            char *szs  = name ? strtok_r(NULL, "\t",     &save_tok) : NULL;
            char *md5  = szs  ? strtok_r(NULL, "\t\r\n", &save_tok) : NULL;
            if (ids && name && szs && md5) {
                dist_file_t *d = &dfiles[dfile_n++];
                d->id   = (uint16_t)atoi(ids);
                strlcpy(d->name, name, sizeof(d->name));
                d->size = (uint32_t)strtoul(szs, NULL, 10);
                strlcpy(d->md5, md5, sizeof(d->md5));
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

static void start_rx(uint16_t fid, uint16_t serial, uint32_t flen, uint16_t total)
{
    rx_reset();
    if (flen > DIST_MAX_FILE) {   // firmware-sized -> Phase 6, skip here
        printfnl(SOURCE_LORA, "dist: id %u too big to RAM-buffer (%u B), skipping\n",
                 (unsigned)fid, (unsigned)flen);
        return;
    }
    rx_file_id = fid; rx_serial = serial; rx_file_len = flen; rx_total = total; rx_have = 0;
    rx_buf    = (uint8_t *)malloc(flen ? flen : 1);
    rx_bitmap = (uint8_t *)calloc((total >> 3) + 1, 1);
    if (!rx_buf || !rx_bitmap) { printfnl(SOURCE_LORA, "dist: alloc fail id %u\n", (unsigned)fid); rx_reset(); }
}

static void add_chunk(uint16_t cidx, const uint8_t *payload, size_t plen)
{
    if (!rx_buf || !rx_bitmap || cidx >= rx_total || bget(rx_bitmap, cidx)) return;
    size_t off = (size_t)cidx * LP_DIST_CHUNK_SIZE;
    if (off + plen > rx_file_len) plen = (off < rx_file_len) ? (rx_file_len - off) : 0;
    if (plen) memcpy(rx_buf + off, payload, plen);
    bset(rx_bitmap, cidx);
    rx_have++;
    if (rx_have >= rx_total) complete();
}

void dist_handle_chunk(const uint8_t *pkt, size_t len)
{
    if (len < LP_DIST_PAYLOAD) return;
    uint16_t serial = lp_rd_u16(pkt + LP_DIST_SERIAL);
    uint16_t fid    = lp_rd_u16(pkt + LP_DIST_FILE_ID);
    uint32_t flen   = lp_rd_u32(pkt + LP_DIST_FILE_LEN);
    uint16_t cidx   = lp_rd_u16(pkt + LP_DIST_CHUNK_IDX);
    uint16_t total  = lp_rd_u16(pkt + LP_DIST_TOTAL_CHUNKS);
    const uint8_t *payload = pkt + LP_DIST_PAYLOAD;
    size_t plen = len - LP_DIST_PAYLOAD;
    dist_chunks_rx++;
    if (total == 0) return;

    if (fid == LP_DIST_MANIFEST_ID) {
        if (have_manifest && serial == cur_serial) return;   // already have this manifest
        if (rx_file_id != LP_DIST_MANIFEST_ID || rx_serial != serial)
            start_rx(LP_DIST_MANIFEST_ID, serial, flen, total);
        add_chunk(cidx, payload, plen);
        return;
    }

    // data-file chunk: need the matching manifest, and the file must be wanted
    if (!have_manifest || serial != cur_serial) return;
    dist_file_t *df = find_file(fid);
    if (!df || !df->wanted) return;
    if (rx_file_id != fid) {
        if (rx_file_id != 0xFFFF) return;     // busy with another reassembly
        start_rx(fid, serial, flen, total);
        if (rx_file_id == fid)
            printfnl(SOURCE_LORA, "dist: fetching %s (%u B, %u chunks)\n",
                     df->name, (unsigned)flen, (unsigned)total);
    }
    add_chunk(cidx, payload, plen);
}

void dist_print_status(void)
{
    if (!have_manifest) { printfnl(SOURCE_COMMANDS, "dist: no manifest heard yet\n"); return; }
    printfnl(SOURCE_COMMANDS, "dist: manifest serial %u  %d files  %u chunks rx  %u done\n",
             (unsigned)cur_serial, dfile_n, (unsigned)dist_chunks_rx, (unsigned)dist_files_done);
    for (int k = 0; k < dfile_n; k++)
        printfnl(SOURCE_COMMANDS, "  [%u] %-22s %6u B  %s  %s\n",
                 (unsigned)dfiles[k].id, dfiles[k].name, (unsigned)dfiles[k].size,
                 dfiles[k].md5, dfiles[k].wanted ? "WANTED" : "ok");
}
