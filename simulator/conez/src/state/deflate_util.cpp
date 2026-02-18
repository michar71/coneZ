#include "deflate_util.h"
#include <zlib.h>
#include <cstring>
#include <cstdlib>

int gzip_stream(const uint8_t *in, size_t in_len,
                deflate_write_fn write_fn, void *ctx)
{
    if (!write_fn) return -1;
    if (!in && in_len > 0) return -1;

    z_stream strm = {};
    strm.next_in = const_cast<Bytef *>(in);
    strm.avail_in = (uInt)in_len;

    /* windowBits = 15 + 16 for gzip output */
    if (deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;

    uint8_t chunk[8192];
    int ret;
    do {
        strm.next_out = chunk;
        strm.avail_out = sizeof(chunk);
        ret = deflate(&strm, strm.avail_in == 0 ? Z_FINISH : Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&strm);
            return -1;
        }
        size_t have = sizeof(chunk) - strm.avail_out;
        if (have > 0 && write_fn(chunk, have, ctx) != 0) {
            deflateEnd(&strm);
            return -1;
        }
    } while (ret != Z_STREAM_END);

    size_t total = strm.total_out;
    deflateEnd(&strm);
    return (int)total;
}

/* ---- gzip_buf: convenience wrapper ---- */

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

int gzip_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    if (!out || out_max == 0) return -1;
    struct buf_ctx ctx = { out, out_max, 0 };
    return gzip_stream(in, in_len, buf_write, &ctx);
}
