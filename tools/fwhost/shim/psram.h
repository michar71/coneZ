// Host shim for firmware/src/psram/psram.h (the ConeZ board's SPI PSRAM).
// Blocks live on the host heap behind the same 0x10000000-based "virtual
// address" scheme; an access outside a live block aborts, so a host import
// that reads or writes past the WASM memory fails the test.
#ifndef FWHOST_PSRAM_H
#define FWHOST_PSRAM_H
#include <stdint.h>
#include <stddef.h>

uint32_t psram_malloc(size_t size);
void     psram_free(uint32_t addr);
void     psram_read(uint32_t addr, uint8_t *buf, size_t len);
void     psram_write(uint32_t addr, const uint8_t *buf, size_t len);
void     psram_memset(uint32_t addr, uint8_t val, size_t len);
void     psram_memcpy(uint32_t dst, uint32_t src, size_t len);
#endif
