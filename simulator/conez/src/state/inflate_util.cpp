#include "inflate_util.h"
#include <zlib.h>
#include <cstring>

int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max)
{
    if (!in || in_len == 0 || !out || out_max == 0) return -1;

    z_stream strm = {};
    strm.next_in = const_cast<Bytef *>(in);
    strm.avail_in = (uInt)in_len;
    strm.next_out = out;
    strm.avail_out = (uInt)out_max;

    // Detect format and choose windowBits:
    //   31       = gzip (auto-detect gzip header)
    //   15       = zlib (standard zlib header)
    //  -15       = raw deflate (no header)
    int windowBits;
    if (in_len >= 10 && in[0] == 0x1F && in[1] == 0x8B) {
        windowBits = 31;    // gzip
    } else if (in_len >= 2) {
        uint8_t cmf = in[0], flg = in[1];
        if ((cmf & 0x0F) == 8 && ((cmf * 256 + flg) % 31) == 0)
            windowBits = 15;  // zlib
        else
            windowBits = -15; // raw deflate
    } else {
        windowBits = -15;     // raw deflate
    }

    if (inflateInit2(&strm, windowBits) != Z_OK)
        return -1;

    int ret = inflate(&strm, Z_FINISH);
    size_t total = strm.total_out;
    inflateEnd(&strm);

    if (ret != Z_STREAM_END)
        return -1;

    return (int)total;
}
