/* ============================================================================
 * deflate.c — Gzip compressor with PSRAM-backed buffers
 * ============================================================================
 * Forked from richgel999/miniz tdefl (MIT license), heavily adapted:
 *   - Large buffers (dict, hash chains, hash heads) in PSRAM via psram_read/write
 *   - Runtime-configurable windowBits, memLevel, compression level
 *   - Streaming output via callback (matches inflate_stream API pattern)
 *   - Gzip (RFC 1952) wrapper around raw deflate stream
 *
 * On boards without PSRAM (Heltec), psram_malloc() falls back to heap and
 * psram_read/write become memcpy — same code path, no #ifdefs.
 * ============================================================================ */

#include "deflate.h"
#include <string.h>
#include <stdlib.h>

/* Forward declarations for PSRAM — avoid including psram.h which has
 * C++ default parameters that break C compilation. */
#ifdef __cplusplus
extern "C" {
#endif
uint32_t psram_malloc(size_t size);
void     psram_free(uint32_t addr);
uint8_t  psram_read8(uint32_t addr);
uint16_t psram_read16(uint32_t addr);
void     psram_write8(uint32_t addr, uint8_t val);
void     psram_write16(uint32_t addr, uint16_t val);
void     psram_read(uint32_t addr, uint8_t *buf, size_t len);
void     psram_write(uint32_t addr, const uint8_t *buf, size_t len);
#ifdef __cplusplus
}
#endif

/* ---- PSRAM accessor macros ---- */
#define DICT_RD8(c, i)     psram_read8((c)->dict_addr + (uint32_t)(i))
#define DICT_WR8(c, i, v)  psram_write8((c)->dict_addr + (uint32_t)(i), (v))
#define HASH_RD16(c, i)    psram_read16((c)->hash_addr + (uint32_t)(i) * 2)
#define HASH_WR16(c, i, v) psram_write16((c)->hash_addr + (uint32_t)(i) * 2, (v))
#define NEXT_RD16(c, i)    psram_read16((c)->next_addr + (uint32_t)(i) * 2)
#define NEXT_WR16(c, i, v) psram_write16((c)->next_addr + (uint32_t)(i) * 2, (v))
#define DICT_READ(c, off, buf, len)  psram_read((c)->dict_addr + (uint32_t)(off), (buf), (len))
#define LZ_SYM_RD(c, i)      psram_read16((c)->lz_sym_addr + (uint32_t)(i) * 2)
#define LZ_SYM_WR(c, i, v)   psram_write16((c)->lz_sym_addr + (uint32_t)(i) * 2, (v))
#define LZ_DIST_RD(c, i)     psram_read16((c)->lz_dist_addr + (uint32_t)(i) * 2)
#define LZ_DIST_WR(c, i, v)  psram_write16((c)->lz_dist_addr + (uint32_t)(i) * 2, (v))

/* ---- Constants ---- */
#define MIN_MATCH     3
#define MAX_MATCH     258
#define END_BLOCK     256
#define MAX_LIT_SYMS  288
#define MAX_DIST_SYMS 32
#define MAX_CL_SYMS   19
#define OUT_BUF_SIZE  4096
#define LZ_MAX_SYMS   2048

/* ---- CRC32 ---- */

static uint32_t s_crc_table[256];
static int s_crc_inited;

static void crc32_init(void)
{
    for (int i = 0; i < 256; i++) {
        uint32_t c = (uint32_t)i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
        s_crc_table[i] = c;
    }
    s_crc_inited = 1;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!s_crc_inited) crc32_init();
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = s_crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ---- Length code tables (RFC 1951) ---- */

static const uint16_t s_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,
    35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t s_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,
    3,3,3,3,4,4,4,4,5,5,5,5,0
};
static uint8_t s_len_sym[259]; /* length (3-258) -> index (0-28) */

/* ---- Distance code tables (RFC 1951) ---- */

static const uint16_t s_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t s_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,
    7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static uint8_t s_dist_sym_lo[513]; /* distance (1-512) -> code */

static int s_tables_inited;

static void init_tables(void)
{
    if (s_tables_inited) return;
    for (int s = 0; s < 29; s++) {
        int count = 1 << s_len_extra[s];
        for (int i = 0; i < count; i++) {
            int l = s_len_base[s] + i;
            if (l <= 258) s_len_sym[l] = (uint8_t)s;
        }
    }
    for (int c = 0; c < 30; c++) {
        int count = 1 << s_dist_extra[c];
        for (int i = 0; i < count; i++) {
            int d = s_dist_base[c] + i;
            if (d <= 512) s_dist_sym_lo[d] = (uint8_t)c;
        }
    }
    s_tables_inited = 1;
}

/* Distance to code: direct LUT for <= 512, log2-based for larger */
static int dist_to_code(int dist)
{
    if (dist <= 512) return s_dist_sym_lo[dist];
    int d = dist - 1, n = 1, tmp = d >> 1;
    while (tmp > 1) { n++; tmp >>= 1; }
    return 2 * (n - 1) + (d >> (n - 1));
}

/* ---- Compressor state ----
 *
 * LZ buffer encoding:
 *   lz_sym[i]  = 0-255 for literal byte, 3-258 for raw match length
 *   lz_dist[i] = 0 for literals, 1-32768 for raw match distance
 * Length/distance symbols and extra bits are computed on the fly during
 * frequency counting and block encoding.
 */

typedef struct {
    /* PSRAM buffer addresses */
    uint32_t dict_addr;      /* sliding window: dict_size bytes */
    uint32_t next_addr;      /* hash chains: dict_size * 2 bytes */
    uint32_t hash_addr;      /* hash heads: hash_size * 2 bytes */

    /* Runtime configuration */
    uint32_t dict_size;
    uint32_t dict_mask;
    uint32_t hash_size;
    uint32_t hash_mask;
    int      max_probes;

    /* Bit writer */
    uint32_t bit_buf;
    int      bits_in;

    /* Output staging (DRAM) */
    uint8_t  out_buf[OUT_BUF_SIZE];
    int      out_pos;

    /* LZ symbols for current block (PSRAM) */
    uint32_t lz_sym_addr;
    uint32_t lz_dist_addr;
    int      lz_count;
    int      lz_cap;

    /* Huffman frequency counts */
    uint16_t lit_freq[MAX_LIT_SYMS];
    uint16_t dist_freq[MAX_DIST_SYMS];

    /* Huffman codes (built per block or fixed) */
    uint16_t lit_code[MAX_LIT_SYMS];
    uint8_t  lit_len[MAX_LIT_SYMS];
    uint16_t dist_code[MAX_DIST_SYMS];
    uint8_t  dist_len[MAX_DIST_SYMS];

    /* Code-length codes for dynamic block header */
    uint16_t cl_code[MAX_CL_SYMS];
    uint8_t  cl_len[MAX_CL_SYMS];

    /* Output callback */
    deflate_write_fn write_fn;
    void            *write_ctx;
    int              total_out;
    int              error;

    /* Dictionary position tracking */
    uint32_t src_pos;
} conez_deflate_t;

/* ---- Output helpers ---- */

static void flush_output(conez_deflate_t *c)
{
    if (c->error || c->out_pos == 0) return;
    if (c->write_fn(c->out_buf, c->out_pos, c->write_ctx) != 0)
        c->error = 1;
    c->total_out += c->out_pos;
    c->out_pos = 0;
}

static void emit_byte(conez_deflate_t *c, uint8_t b)
{
    if (c->out_pos >= OUT_BUF_SIZE) flush_output(c);
    c->out_buf[c->out_pos++] = b;
}

/* ---- Bit writer (LSB-first, per RFC 1951) ---- */

static void put_bits(conez_deflate_t *c, uint32_t bits, int count)
{
    c->bit_buf |= bits << c->bits_in;
    c->bits_in += count;
    while (c->bits_in >= 8) {
        emit_byte(c, (uint8_t)(c->bit_buf & 0xFF));
        c->bit_buf >>= 8;
        c->bits_in -= 8;
    }
}

static void flush_bits(conez_deflate_t *c)
{
    if (c->bits_in > 0)
        emit_byte(c, (uint8_t)(c->bit_buf & 0xFF));
    c->bit_buf = 0;
    c->bits_in = 0;
}

/* ---- Canonical Huffman code generation ---- */

static uint16_t bit_reverse(uint16_t code, int len)
{
    uint16_t r = 0;
    for (int i = 0; i < len; i++)
        r |= ((code >> i) & 1) << (len - 1 - i);
    return r;
}

static void gen_codes(const uint8_t *lens, uint16_t *codes, int n)
{
    int bl_count[16] = {0};
    for (int i = 0; i < n; i++)
        if (lens[i]) bl_count[lens[i]]++;

    int next_code[16];
    int code = 0;
    for (int bits = 1; bits <= 15; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    for (int i = 0; i < n; i++) {
        if (lens[i] == 0) { codes[i] = 0; continue; }
        codes[i] = bit_reverse((uint16_t)next_code[lens[i]]++, lens[i]);
    }
}

/* ---- Huffman tree builder ----
 * Build optimal code lengths from symbol frequencies, limited to max_bits.
 * Uses sorted-input two-queue merge (O(n log n) sort, O(n) merge). */

/* Heap-allocated workspace for build_tree (~7KB — too large for 8KB ShellTask stack) */
typedef struct {
    struct { uint32_t freq; int sym; } sorted[MAX_LIT_SYMS];
    int16_t  parent[MAX_LIT_SYMS * 2];
    uint32_t nfreq[MAX_LIT_SYMS * 2];
    int      q2_buf[MAX_LIT_SYMS];
} build_tree_ws;

static void build_tree(const uint16_t *freqs, uint8_t *lens, uint16_t *codes,
                       int n, int max_bits)
{
    memset(lens, 0, n);

    /* Quick scan: count active symbols before allocating */
    int m = 0;
    for (int i = 0; i < n; i++)
        if (freqs[i] > 0) m++;

    if (m == 0) { memset(codes, 0, n * sizeof(uint16_t)); return; }

    build_tree_ws *ws = (build_tree_ws *)malloc(sizeof(build_tree_ws));
    if (!ws) {
        /* Fallback: assign uniform 8-bit codes (valid but suboptimal) */
        for (int i = 0; i < n; i++) lens[i] = freqs[i] ? 8 : 0;
        gen_codes(lens, codes, n);
        return;
    }

    m = 0;
    for (int i = 0; i < n; i++)
        if (freqs[i] > 0) { ws->sorted[m].freq = freqs[i]; ws->sorted[m].sym = i; m++; }

    if (m == 1) {
        lens[ws->sorted[0].sym] = 1;
        memset(codes, 0, n * sizeof(uint16_t));
        codes[ws->sorted[0].sym] = 0;
        free(ws);
        return;
    }

    /* Insertion sort by frequency */
    for (int i = 1; i < m; i++) {
        typeof(ws->sorted[0]) tmp = ws->sorted[i];
        int j = i;
        while (j > 0 && ws->sorted[j-1].freq > tmp.freq) { ws->sorted[j] = ws->sorted[j-1]; j--; }
        ws->sorted[j] = tmp;
    }

    /* Build tree with parent pointers.
     * Nodes 0..m-1 are leaves (sorted), m..2m-2 are internal. */
    for (int i = 0; i < m; i++) { ws->nfreq[i] = ws->sorted[i].freq; ws->parent[i] = -1; }

    int q1 = 0, q2h = 0, q2t = 0;
    int nn = m;

    for (int step = 0; step < m - 1; step++) {
        int ch[2];
        for (int p = 0; p < 2; p++) {
            uint32_t f1 = (q1 < m) ? ws->nfreq[q1] : 0xFFFFFFFFu;
            uint32_t f2 = (q2h < q2t) ? ws->nfreq[ws->q2_buf[q2h]] : 0xFFFFFFFFu;
            if (f1 <= f2) ch[p] = q1++;
            else          ch[p] = ws->q2_buf[q2h++];
        }
        ws->nfreq[nn] = ws->nfreq[ch[0]] + ws->nfreq[ch[1]];
        ws->parent[nn] = -1;
        ws->parent[ch[0]] = (int16_t)nn;
        ws->parent[ch[1]] = (int16_t)nn;
        ws->q2_buf[q2t++] = nn;
        nn++;
    }

    /* Extract leaf depths */
    for (int i = 0; i < m; i++) {
        int d = 0, p = i;
        while (ws->parent[p] != -1) { d++; p = ws->parent[p]; }
        lens[ws->sorted[i].sym] = (uint8_t)(d <= max_bits ? d : max_bits);
    }

    /* Enforce max_bits via bl_count redistribution (rare for n<=288, max=15) */
    {
        int bl_count[16] = {0};
        for (int i = 0; i < n; i++)
            if (lens[i]) bl_count[lens[i]]++;

        int overflow = 0;
        for (int b = max_bits; b >= 1; b--) {
            overflow += bl_count[b];
            overflow >>= 1;
        }
        overflow -= 1;

        while (overflow > 0) {
            int found = 0;
            for (int b = max_bits - 1; b >= 1 && !found; b--) {
                if (bl_count[b] > 0) {
                    bl_count[b]--;
                    bl_count[b + 1] += 2;
                    bl_count[max_bits]--;
                    overflow--;
                    found = 1;
                }
            }
            if (!found) break;
        }

        int bits = max_bits;
        for (int i = 0; i < m; i++) {
            while (bits >= 1 && bl_count[bits] == 0) bits--;
            if (bits < 1) break;
            lens[ws->sorted[i].sym] = (uint8_t)bits;
            bl_count[bits]--;
        }
    }

    free(ws);
    gen_codes(lens, codes, n);
}

/* ---- Fixed Huffman tables ---- */

static uint8_t  s_fixed_lit_len[MAX_LIT_SYMS];
static uint16_t s_fixed_lit_code[MAX_LIT_SYMS];
static uint8_t  s_fixed_dist_len[MAX_DIST_SYMS];
static uint16_t s_fixed_dist_code[MAX_DIST_SYMS];
static int s_fixed_inited;

static void init_fixed_huffman(void)
{
    if (s_fixed_inited) return;
    for (int i =   0; i <= 143; i++) s_fixed_lit_len[i] = 8;
    for (int i = 144; i <= 255; i++) s_fixed_lit_len[i] = 9;
    for (int i = 256; i <= 279; i++) s_fixed_lit_len[i] = 7;
    for (int i = 280; i <= 287; i++) s_fixed_lit_len[i] = 8;
    gen_codes(s_fixed_lit_len, s_fixed_lit_code, MAX_LIT_SYMS);
    for (int i = 0; i < MAX_DIST_SYMS; i++) s_fixed_dist_len[i] = 5;
    gen_codes(s_fixed_dist_len, s_fixed_dist_code, MAX_DIST_SYMS);
    s_fixed_inited = 1;
}

/* ---- Block encoding ---- */

static void count_frequencies(conez_deflate_t *c)
{
    memset(c->lit_freq, 0, sizeof(c->lit_freq));
    memset(c->dist_freq, 0, sizeof(c->dist_freq));
    c->lit_freq[END_BLOCK] = 1;

    for (int i = 0; i < c->lz_count; i++) {
        uint16_t v = LZ_SYM_RD(c, i);
        uint16_t d = LZ_DIST_RD(c, i);
        if (d == 0) {
            c->lit_freq[v]++;
        } else {
            c->lit_freq[257 + s_len_sym[v]]++;
            c->dist_freq[dist_to_code(d)]++;
        }
    }
}

static void emit_block_symbols(conez_deflate_t *c,
                               const uint16_t *lc, const uint8_t *ll,
                               const uint16_t *dc, const uint8_t *dl)
{
    for (int i = 0; i < c->lz_count; i++) {
        uint16_t v = LZ_SYM_RD(c, i);
        uint16_t d = LZ_DIST_RD(c, i);
        if (d == 0) {
            put_bits(c, lc[v], ll[v]);
        } else {
            int lidx = s_len_sym[v];
            int lsym = 257 + lidx;
            put_bits(c, lc[lsym], ll[lsym]);
            if (s_len_extra[lidx] > 0)
                put_bits(c, v - s_len_base[lidx], s_len_extra[lidx]);
            int dist = d;
            int dsym = dist_to_code(dist);
            put_bits(c, dc[dsym], dl[dsym]);
            if (s_dist_extra[dsym] > 0)
                put_bits(c, dist - s_dist_base[dsym], s_dist_extra[dsym]);
        }
    }
    put_bits(c, lc[END_BLOCK], ll[END_BLOCK]);
}

/* ---- Dynamic block header (RFC 1951 section 3.2.7) ---- */

static const uint8_t s_cl_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static void emit_dynamic_block(conez_deflate_t *c, int is_final)
{
    count_frequencies(c);

    /* Ensure at least 1 distance code */
    {
        int has_dist = 0;
        for (int i = 0; i < MAX_DIST_SYMS; i++)
            if (c->dist_freq[i]) { has_dist = 1; break; }
        if (!has_dist) c->dist_freq[0] = 1;
    }

    build_tree(c->lit_freq, c->lit_len, c->lit_code, MAX_LIT_SYMS, 15);
    build_tree(c->dist_freq, c->dist_len, c->dist_code, MAX_DIST_SYMS, 15);

    /* Determine HLIT and HDIST (trim trailing zeros) */
    int hlit = MAX_LIT_SYMS;
    while (hlit > 257 && c->lit_len[hlit - 1] == 0) hlit--;
    int hdist = MAX_DIST_SYMS;
    while (hdist > 1 && c->dist_len[hdist - 1] == 0) hdist--;

    /* Combine code lengths and RLE encode */
    uint8_t combined[MAX_LIT_SYMS + MAX_DIST_SYMS];
    memcpy(combined, c->lit_len, hlit);
    memcpy(combined + hlit, c->dist_len, hdist);
    int combined_len = hlit + hdist;

    uint8_t rle_syms[MAX_LIT_SYMS + MAX_DIST_SYMS + 64];
    uint8_t rle_extra[MAX_LIT_SYMS + MAX_DIST_SYMS + 64];
    uint16_t cl_freq[MAX_CL_SYMS];
    memset(cl_freq, 0, sizeof(cl_freq));
    int rle_count = 0;

    for (int i = 0; i < combined_len; ) {
        int val = combined[i];
        int run = 1;
        while (i + run < combined_len && combined[i + run] == val) run++;

        if (val == 0 && run >= 3) {
            while (run >= 3) {
                if (run >= 11) {
                    int r = run > 138 ? 138 : run;
                    rle_syms[rle_count] = 18; rle_extra[rle_count] = (uint8_t)(r - 11);
                    cl_freq[18]++; rle_count++; i += r; run -= r;
                } else {
                    int r = run > 10 ? 10 : run;
                    rle_syms[rle_count] = 17; rle_extra[rle_count] = (uint8_t)(r - 3);
                    cl_freq[17]++; rle_count++; i += r; run -= r;
                }
            }
            while (run > 0) {
                rle_syms[rle_count] = 0; rle_extra[rle_count] = 0;
                cl_freq[0]++; rle_count++; i++; run--;
            }
        } else {
            rle_syms[rle_count] = (uint8_t)val; rle_extra[rle_count] = 0;
            cl_freq[val]++; rle_count++; i++; run--;
            while (run >= 3) {
                int r = run > 6 ? 6 : run;
                rle_syms[rle_count] = 16; rle_extra[rle_count] = (uint8_t)(r - 3);
                cl_freq[16]++; rle_count++; i += r; run -= r;
            }
            while (run > 0) {
                rle_syms[rle_count] = (uint8_t)val; rle_extra[rle_count] = 0;
                cl_freq[val]++; rle_count++; i++; run--;
            }
        }
    }

    /* Build code-length Huffman tree */
    build_tree(cl_freq, c->cl_len, c->cl_code, MAX_CL_SYMS, 7);

    int hclen = MAX_CL_SYMS;
    while (hclen > 4 && c->cl_len[s_cl_order[hclen - 1]] == 0) hclen--;

    /* Emit block header */
    put_bits(c, is_final ? 1 : 0, 1);
    put_bits(c, 2, 2);  /* BTYPE = dynamic */
    put_bits(c, hlit - 257, 5);
    put_bits(c, hdist - 1, 5);
    put_bits(c, hclen - 4, 4);

    for (int i = 0; i < hclen; i++)
        put_bits(c, c->cl_len[s_cl_order[i]], 3);

    for (int i = 0; i < rle_count; i++) {
        int sym = rle_syms[i];
        put_bits(c, c->cl_code[sym], c->cl_len[sym]);
        if (sym == 16)      put_bits(c, rle_extra[i], 2);
        else if (sym == 17) put_bits(c, rle_extra[i], 3);
        else if (sym == 18) put_bits(c, rle_extra[i], 7);
    }

    emit_block_symbols(c, c->lit_code, c->lit_len, c->dist_code, c->dist_len);
}

static void __attribute__((unused)) emit_fixed_block(conez_deflate_t *c, int is_final)
{
    init_fixed_huffman();
    put_bits(c, is_final ? 1 : 0, 1);
    put_bits(c, 1, 2);  /* BTYPE = fixed */
    emit_block_symbols(c, s_fixed_lit_code, s_fixed_lit_len,
                       s_fixed_dist_code, s_fixed_dist_len);
}

static void emit_stored_block(conez_deflate_t *c, const uint8_t *data,
                              size_t len, int is_final)
{
    put_bits(c, is_final ? 1 : 0, 1);
    put_bits(c, 0, 2);  /* BTYPE = stored */
    flush_bits(c);
    uint16_t blen = (uint16_t)len;
    uint16_t nlen = ~blen;
    emit_byte(c, blen & 0xFF); emit_byte(c, blen >> 8);
    emit_byte(c, nlen & 0xFF); emit_byte(c, nlen >> 8);
    for (size_t i = 0; i < len; i++) emit_byte(c, data[i]);
}

static void flush_block(conez_deflate_t *c, int is_final, int level,
                        const uint8_t *raw_data, size_t raw_len)
{
    if (level == 0 || c->lz_count == 0) {
        size_t off = 0;
        while (off < raw_len) {
            size_t chunk = raw_len - off;
            if (chunk > 65535) chunk = 65535;
            emit_stored_block(c, raw_data + off, chunk,
                              is_final && off + chunk >= raw_len);
            off += chunk;
        }
        if (raw_len == 0 && is_final)
            emit_stored_block(c, NULL, 0, 1);
    } else {
        emit_dynamic_block(c, is_final);
    }
    c->lz_count = 0;
}

/* ---- LZ77 match finder ---- */

static uint32_t hash3(conez_deflate_t *c, uint8_t b0, uint8_t b1, uint8_t b2)
{
    return ((uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16)) & c->hash_mask;
}

static void hash_insert(conez_deflate_t *c, uint32_t pos,
                        uint8_t b0, uint8_t b1, uint8_t b2)
{
    uint32_t h = hash3(c, b0, b1, b2);
    uint16_t prev = HASH_RD16(c, h);
    NEXT_WR16(c, pos & c->dict_mask, prev);
    HASH_WR16(c, h, (uint16_t)(pos & c->dict_mask));
}

static int find_match(conez_deflate_t *c, const uint8_t *cur_buf, int avail,
                      uint32_t cur_pos, int *match_dist)
{
    if (avail < MIN_MATCH) return 0;

    uint32_t h = hash3(c, cur_buf[0], cur_buf[1], cur_buf[2]);
    uint16_t candidate = HASH_RD16(c, h);
    int best_len = MIN_MATCH - 1;
    int best_dist = 0;
    int max_len = avail < MAX_MATCH ? avail : MAX_MATCH;
    int probes = c->max_probes;
    uint32_t dict_pos = cur_pos & c->dict_mask;

    while (candidate != dict_pos && probes-- > 0) {
        int dist = (int)((dict_pos - candidate) & c->dict_mask);
        if (dist == 0 || (uint32_t)dist > c->src_pos || (uint32_t)dist > c->dict_size)
            break;

        /* Bulk-read candidate bytes into DRAM for comparison */
        uint8_t probe_buf[MAX_MATCH];
        int read_len = max_len;
        if ((uint32_t)candidate + read_len <= c->dict_size) {
            DICT_READ(c, candidate, probe_buf, read_len);
        } else {
            int first = (int)(c->dict_size - candidate);
            DICT_READ(c, candidate, probe_buf, first);
            DICT_READ(c, 0, probe_buf + first, read_len - first);
        }

        if (probe_buf[0] == cur_buf[0] && probe_buf[best_len] == cur_buf[best_len]) {
            int len = 0;
            while (len < max_len && probe_buf[len] == cur_buf[len]) len++;
            if (len > best_len) {
                best_len = len;
                best_dist = dist;
                if (len >= max_len) break;
            }
        }

        uint16_t next_cand = NEXT_RD16(c, candidate);
        if (next_cand == candidate) break;
        candidate = next_cand;
    }

    if (best_len >= MIN_MATCH) { *match_dist = best_dist; return best_len; }
    return 0;
}

/* ---- Main compression loop ---- */

static void compress_data(conez_deflate_t *c, const uint8_t *in, size_t in_len,
                          int level)
{
    if (in_len == 0 || level == 0) {
        flush_block(c, 1, level, in, in_len);
        return;
    }

    const uint8_t *block_start = in;
    size_t block_raw_len = 0;
    size_t pos = 0;

    while (pos < in_len && !c->error) {
        int avail = (int)(in_len - pos);
        if (avail > MAX_MATCH) avail = MAX_MATCH;
        uint8_t lookahead[MAX_MATCH];
        memcpy(lookahead, in + pos, avail);

        uint32_t dpos = c->src_pos & c->dict_mask;
        DICT_WR8(c, dpos, in[pos]);

        if (avail >= MIN_MATCH)
            hash_insert(c, c->src_pos, in[pos], in[pos + 1], in[pos + 2]);

        int match_dist = 0;
        int match_len = find_match(c, lookahead, avail, c->src_pos, &match_dist);

        if (match_len >= MIN_MATCH) {
            LZ_SYM_WR(c, c->lz_count, (uint16_t)match_len);
            LZ_DIST_WR(c, c->lz_count, (uint16_t)match_dist);
            c->lz_count++;

            for (int j = 1; j < match_len; j++) {
                c->src_pos++; pos++;
                DICT_WR8(c, c->src_pos & c->dict_mask, in[pos]);
                if (pos + 2 < in_len)
                    hash_insert(c, c->src_pos, in[pos], in[pos + 1], in[pos + 2]);
            }
            block_raw_len += match_len;
        } else {
            LZ_SYM_WR(c, c->lz_count, in[pos]);
            LZ_DIST_WR(c, c->lz_count, 0);
            c->lz_count++;
            block_raw_len++;
        }

        c->src_pos++; pos++;

        if (c->lz_count >= c->lz_cap - 2) {
            flush_block(c, pos >= in_len, level, block_start, block_raw_len);
            block_start = in + pos;
            block_raw_len = 0;
        }
    }

    if (c->lz_count > 0 && !c->error)
        flush_block(c, 1, level, block_start, block_raw_len);
}

/* ---- Allocate / free compressor ---- */

static const int s_probe_table[11] = {
    0, 1, 2, 4, 8, 32, 128, 256, 512, 1024, 4095
};

static conez_deflate_t *deflate_alloc(int window_bits, int mem_level, int level,
                                       deflate_write_fn write_fn, void *ctx)
{
    if (window_bits < 9)  window_bits = 9;
    if (window_bits > 15) window_bits = 15;
    if (mem_level < 1) mem_level = 1;
    if (mem_level > 9) mem_level = 9;
    if (level < 0) level = 0;
    if (level > 10) level = 10;

    int hash_bits = mem_level + 6;
    if (hash_bits < 7)  hash_bits = 7;
    if (hash_bits > 15) hash_bits = 15;

    uint32_t dict_size = 1u << window_bits;
    uint32_t hash_size = 1u << hash_bits;

    conez_deflate_t *c = (conez_deflate_t *)calloc(1, sizeof(conez_deflate_t));
    if (!c) return NULL;

    c->dict_size = dict_size;
    c->dict_mask = dict_size - 1;
    c->hash_size = hash_size;
    c->hash_mask = hash_size - 1;
    c->max_probes = s_probe_table[level];
    c->write_fn = write_fn;
    c->write_ctx = ctx;

    c->lz_cap = LZ_MAX_SYMS;
    c->lz_sym_addr  = psram_malloc(c->lz_cap * 2);
    c->lz_dist_addr = psram_malloc(c->lz_cap * 2);
    if (!c->lz_sym_addr || !c->lz_dist_addr) goto fail;

    if (level > 0) {
        c->dict_addr = psram_malloc(dict_size);
        c->next_addr = psram_malloc(dict_size * 2);
        c->hash_addr = psram_malloc(hash_size * 2);
        if (!c->dict_addr || !c->next_addr || !c->hash_addr) goto fail;

        /* Zero the hash and chain tables */
        uint8_t zeros[256];
        memset(zeros, 0, sizeof(zeros));
        for (uint32_t off = 0; off < hash_size * 2; off += sizeof(zeros)) {
            uint32_t chunk = hash_size * 2 - off;
            if (chunk > sizeof(zeros)) chunk = sizeof(zeros);
            psram_write(c->hash_addr + off, zeros, chunk);
        }
        for (uint32_t off = 0; off < dict_size * 2; off += sizeof(zeros)) {
            uint32_t chunk = dict_size * 2 - off;
            if (chunk > sizeof(zeros)) chunk = sizeof(zeros);
            psram_write(c->next_addr + off, zeros, chunk);
        }
    }

    return c;

fail:
    if (c->lz_sym_addr) psram_free(c->lz_sym_addr);
    if (c->lz_dist_addr) psram_free(c->lz_dist_addr);
    if (c->dict_addr) psram_free(c->dict_addr);
    if (c->next_addr) psram_free(c->next_addr);
    if (c->hash_addr) psram_free(c->hash_addr);
    free(c);
    return NULL;
}

static void deflate_free(conez_deflate_t *c)
{
    if (!c) return;
    if (c->lz_sym_addr) psram_free(c->lz_sym_addr);
    if (c->lz_dist_addr) psram_free(c->lz_dist_addr);
    if (c->dict_addr) psram_free(c->dict_addr);
    if (c->next_addr) psram_free(c->next_addr);
    if (c->hash_addr) psram_free(c->hash_addr);
    free(c);
}

/* ---- Public API ---- */

int gzip_stream(const uint8_t *in, size_t in_len,
                deflate_write_fn write_fn, void *ctx,
                int window_bits, int mem_level, int level)
{
    if (!write_fn) return -1;
    if (!in && in_len > 0) return -1;

    init_tables();

    conez_deflate_t *c = deflate_alloc(window_bits, mem_level, level, write_fn, ctx);
    if (!c) return -1;

    /* Gzip header (RFC 1952): 10 bytes, minimal */
    static const uint8_t gz_hdr[10] = {
        0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF
    };
    if (write_fn(gz_hdr, 10, ctx) != 0) { deflate_free(c); return -1; }
    c->total_out = 10;

    uint32_t crc = crc32_update(0, in ? in : (const uint8_t *)"", in_len);

    compress_data(c, in, in_len, level);
    flush_bits(c);
    flush_output(c);

    if (c->error) { deflate_free(c); return -1; }

    /* Gzip trailer: CRC32 + ISIZE (little-endian) */
    uint8_t trailer[8];
    uint32_t isize = (uint32_t)(in_len & 0xFFFFFFFF);
    trailer[0] = crc; trailer[1] = crc >> 8; trailer[2] = crc >> 16; trailer[3] = crc >> 24;
    trailer[4] = isize; trailer[5] = isize >> 8; trailer[6] = isize >> 16; trailer[7] = isize >> 24;
    if (write_fn(trailer, 8, ctx) != 0) { deflate_free(c); return -1; }

    int total = c->total_out + 8;
    deflate_free(c);
    return total;
}

/* ---- gzip_buf: convenience wrapper ---- */

struct buf_ctx { uint8_t *out; size_t max, pos; };

static int buf_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    struct buf_ctx *b = (struct buf_ctx *)ctx;
    if (b->pos + len > b->max) return -1;
    memcpy(b->out + b->pos, data, len);
    b->pos += len;
    return 0;
}

int gzip_buf(const uint8_t *in, size_t in_len,
             uint8_t *out, size_t out_max,
             int window_bits, int mem_level, int level)
{
    if (!out || out_max == 0) return -1;
    struct buf_ctx ctx = { out, out_max, 0 };
    return gzip_stream(in, in_len, buf_write_cb, &ctx, window_bits, mem_level, level);
}
