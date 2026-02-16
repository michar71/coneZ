#include "inflate_util.h"
#include <zlib.h>
#include <cstring>
#include <cstdlib>

static int detect_window_bits(const uint8_t *in, size_t in_len)
{
    if (in_len >= 10 && in[0] == 0x1F && in[1] == 0x8B)
        return 31;    // gzip
    if (in_len >= 2) {
        uint8_t cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) == 8 && ((cmf * 256 + flg) % 31) == 0)
            return 15;  // zlib
    }
    return -15;         // raw deflate
}

int inflate_stream(const uint8_t *in, size_t in_len,
                   inflate_write_fn write_fn, void *ctx)
{
    if (!in || in_len == 0 || !write_fn) return -1;

    z_stream strm = {};
    strm.next_in = const_cast<Bytef *>(in);
    strm.avail_in = (uInt)in_len;

    if (inflateInit2(&strm, detect_window_bits(in, in_len)) != Z_OK)
        return -1;

    uint8_t chunk[8192];
    int ret;
    do {
        strm.next_out = chunk;
        strm.avail_out = sizeof(chunk);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret < 0 && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return -1;
        }
        size_t have = sizeof(chunk) - strm.avail_out;
        if (have > 0 && write_fn(chunk, have, ctx) != 0) {
            inflateEnd(&strm);
            return -1;
        }
    } while (ret != Z_STREAM_END);

    size_t total = strm.total_out;
    inflateEnd(&strm);
    return (int)total;
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
