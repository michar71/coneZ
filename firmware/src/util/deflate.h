#ifndef deflate_h
#define deflate_h

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Callback for streaming compression.  Called with compressed output chunks.
 * Return 0 on success, -1 on error (aborts compression).
 */
typedef int (*deflate_write_fn)(const uint8_t *data, size_t len, void *ctx);

/*
 * Streaming gzip compress: wraps raw deflate in a gzip container (RFC 1952).
 * Calls write_fn with compressed output chunks.
 *
 * window_bits: 9-15 (dict size = 1 << window_bits)
 * mem_level:   1-9  (hash table bits = mem_level + 6)
 * level:       0-10 (0=stored, 1=fastest, 6=default, 10=best)
 *
 * Large buffers (dict, hash chains, hash heads) are allocated via
 * psram_malloc() — PSRAM on ConeZ PCB, heap fallback on Heltec.
 *
 * Returns total compressed size on success, -1 on error.
 */
int gzip_stream(const uint8_t *in, size_t in_len,
                deflate_write_fn write_fn, void *ctx,
                int window_bits, int mem_level, int level);

/*
 * Convenience: gzip compress entirely into a memory buffer.
 * Returns compressed size on success, -1 on error or overflow.
 */
int gzip_buf(const uint8_t *in, size_t in_len,
             uint8_t *out, size_t out_max,
             int window_bits, int mem_level, int level);

#ifdef __cplusplus
}
#endif

#endif
