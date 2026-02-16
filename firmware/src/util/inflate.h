#ifndef inflate_h
#define inflate_h

#include <stdint.h>
#include <stddef.h>

/*
 * Decompress gzip, zlib, or raw deflate data in memory.
 * Auto-detects format from header bytes.
 * Returns decompressed size on success, -1 on error.
 * Uses ROM tinfl_decompress with a heap-allocated 32KB dictionary.
 */
int inflate_buf(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max);

#endif
