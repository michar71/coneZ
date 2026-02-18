#ifndef deflate_util_h
#define deflate_util_h

#include <cstdint>
#include <cstddef>

/*
 * Callback for streaming compression.  Called with compressed output chunks.
 * Return 0 on success, -1 on error (aborts compression).
 */
typedef int (*deflate_write_fn)(const uint8_t *data, size_t len, void *ctx);

/*
 * Streaming gzip compress: wraps raw deflate in a gzip container (RFC 1952).
 * Calls write_fn with compressed output chunks.  Uses zlib deflateInit2().
 * Returns total compressed size on success, -1 on error.
 */
int gzip_stream(const uint8_t *in, size_t in_len,
                deflate_write_fn write_fn, void *ctx);

/*
 * Convenience: gzip compress entirely into a memory buffer.
 * Returns compressed size on success, -1 on error or overflow.
 */
int gzip_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#endif
