#include "inflate.h"
#include <rom/miniz.h>
#include <stdlib.h>
#include <string.h>

/*
 * Detect format and skip to raw deflate data.
 * Sets *flags to tinfl flags (TINFL_FLAG_PARSE_ZLIB_HEADER for zlib, 0 otherwise).
 * Returns pointer to deflate stream start, updates *data_len to deflate-only length.
 * Returns NULL on truncated header.
 */
static const uint8_t *detect_format(const uint8_t *in, size_t in_len,
                                     size_t *data_len, uint32_t *flags)
{
    *flags = 0;

    // Gzip: 1F 8B 08
    if (in_len >= 10 && in[0] == 0x1F && in[1] == 0x8B && in[2] == 0x08) {
        uint8_t flg = in[3];
        size_t off = 10;
        if (flg & 0x04) { // FEXTRA
            if (off + 2 > in_len) return NULL;
            uint16_t xlen = in[off] | (in[off + 1] << 8);
            off += 2 + xlen;
        }
        if (flg & 0x08) { // FNAME
            while (off < in_len && in[off]) off++;
            off++;
        }
        if (flg & 0x10) { // FCOMMENT
            while (off < in_len && in[off]) off++;
            off++;
        }
        if (flg & 0x02) off += 2; // FHCRC
        if (off >= in_len) return NULL;
        *data_len = in_len - off - 8; // 8-byte trailer (CRC32 + ISIZE)
        return in + off;
    }

    // Zlib: CMF byte with method 8, CMF*256+FLG divisible by 31
    if (in_len >= 2) {
        uint8_t cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) == 8 && ((cmf * 256 + flg) % 31) == 0) {
            *flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
            *data_len = in_len;
            return in;
        }
    }

    // Raw deflate
    *data_len = in_len;
    return in;
}

int inflate_stream(const uint8_t *in, size_t in_len,
                   inflate_write_fn write_fn, void *ctx)
{
    if (!in || in_len == 0 || !write_fn) return -1;

    size_t data_len;
    uint32_t tinfl_flags;
    const uint8_t *deflate_data = detect_format(in, in_len, &data_len, &tinfl_flags);
    if (!deflate_data) return -1;

    // Heap-allocate dictionary + decompressor (can't use stack on 8KB ShellTask)
    uint8_t *dict = (uint8_t *)malloc(TINFL_LZ_DICT_SIZE);
    if (!dict) return -1;

    tinfl_decompressor *decomp = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    if (!decomp) { free(dict); return -1; }
    tinfl_init(decomp);

    const uint8_t *in_ptr = deflate_data;
    size_t in_remaining = data_len;
    size_t dict_ofs = 0;
    size_t total_out = 0;
    int result = -1;

    for (;;) {
        size_t in_bytes = in_remaining;
        size_t out_bytes = TINFL_LZ_DICT_SIZE - dict_ofs;
        uint32_t flags = tinfl_flags;
        if (in_remaining > 0) flags |= TINFL_FLAG_HAS_MORE_INPUT;

        tinfl_status status = tinfl_decompress(decomp, in_ptr, &in_bytes,
            dict, dict + dict_ofs, &out_bytes, flags);

        in_ptr += in_bytes;
        in_remaining -= in_bytes;

        if (out_bytes > 0) {
            if (write_fn(dict + dict_ofs, out_bytes, ctx) != 0) break;
            total_out += out_bytes;
            dict_ofs = (dict_ofs + out_bytes) & (TINFL_LZ_DICT_SIZE - 1);
        }

        if (status == TINFL_STATUS_DONE) { result = (int)total_out; break; }
        if (status < 0) break;
    }

    free(decomp);
    free(dict);
    return result;
}

// --- inflate_buf: convenience wrapper ---

struct buf_ctx {
    uint8_t *out;
    size_t max;
    size_t pos;
};

static int buf_write(const uint8_t *data, size_t len, void *ctx)
{
    struct buf_ctx *b = (struct buf_ctx *)ctx;
    if (b->pos + len > b->max) return -1;
    memcpy(b->out + b->pos, data, len);
    b->pos += len;
    return 0;
}

int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    if (!out || out_max == 0) return -1;
    struct buf_ctx ctx = { out, out_max, 0 };
    return inflate_stream(in, in_len, buf_write, &ctx);
}
