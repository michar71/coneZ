#ifndef inflate_util_h
#define inflate_util_h

#include <cstdint>
#include <cstddef>

/*
 * Callback for streaming decompression.  Called once per output chunk.
 * Return 0 on success, -1 on error (aborts decompression).
 */
typedef int (*inflate_write_fn)(const uint8_t *data, size_t len, void *ctx);

/*
 * Streaming decompress: auto-detects gzip/zlib/raw deflate, calls write_fn
 * for each decompressed chunk.  Uses zlib inflateInit2()/inflate().
 * Returns total decompressed size on success, -1 on error.
 */
int inflate_stream(const uint8_t *in, size_t in_len,
                   inflate_write_fn write_fn, void *ctx);

/*
 * Convenience: decompress entirely into a memory buffer.
 * Returns decompressed size on success, -1 on error or overflow.
 */
int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#endif
