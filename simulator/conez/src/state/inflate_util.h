#ifndef inflate_util_h
#define inflate_util_h

#include <cstdint>
#include <cstddef>

/*
 * Decompress gzip, zlib, or raw deflate data in memory.
 * Auto-detects format from header bytes.
 * Returns decompressed size on success, -1 on error.
 * Uses zlib inflateInit2()/inflate().
 */
int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#endif
