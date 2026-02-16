#ifndef inflate_h
#define inflate_h

#include <stdint.h>
#include <stddef.h>

/*
 * Callback for streaming decompression.  Called once per output chunk
 * (up to 32KB each, from the tinfl dictionary window).
 * Return 0 on success, -1 on error (aborts decompression).
 */
typedef int (*inflate_write_fn)(const uint8_t *data, size_t len, void *ctx);

/*
 * Streaming decompress: auto-detects gzip/zlib/raw deflate, calls write_fn
 * for each decompressed chunk.  Uses a heap-allocated 32KB dictionary.
 * Returns total decompressed size on success, -1 on error.
 *
 * Peak RAM: input buffer (caller) + 32KB dict + ~11KB tinfl state.
 */
int inflate_stream(const uint8_t *in, size_t in_len,
                   inflate_write_fn write_fn, void *ctx);

/*
 * Convenience: decompress entirely into a memory buffer.
 * Returns decompressed size on success, -1 on error or overflow.
 */
int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#endif
