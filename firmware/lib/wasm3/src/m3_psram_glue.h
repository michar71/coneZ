//
//  m3_psram_glue.h
//
//  Platform-provided functions for routing WASM linear memory through SPI PSRAM.
//  Declared here (no psram.h dependency) so the wasm3 fork stays clean.
//  Implemented in firmware/src/wasm/wasm_psram_glue.cpp.
//

#ifndef m3_psram_glue_h
#define m3_psram_glue_h

#if d_m3UsePsramMemory

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void     m3_psram_read   (uint32_t addr, uint8_t *buf, size_t len);
void     m3_psram_write  (uint32_t addr, const uint8_t *buf, size_t len);
void     m3_psram_memset (uint32_t addr, uint8_t val, size_t len);
void     m3_psram_memcpy (uint32_t dst_addr, uint32_t src_addr, size_t len);
uint32_t m3_psram_alloc  (size_t size);
void     m3_psram_free   (uint32_t addr);

// Split-aware helpers for bulk ops that may straddle the DRAM/PSRAM boundary.
// dram_buf = DRAM fast-path buffer, psram_addr = PSRAM base for data beyond window.
void     m3_split_read   (uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, uint8_t *dst, uint32_t len);
void     m3_split_write  (uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, const uint8_t *src, uint32_t len);
void     m3_split_set    (uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, uint8_t val, uint32_t len);
void     m3_split_move   (uint8_t *dram_buf, uint32_t psram_addr, uint32_t dst_off, uint32_t src_off, uint32_t len);

// Yield counter for PSRAM load/store path — must be extern because m3_exec.h
// is included (as static inline) in multiple translation units.
extern uint32_t m3_psram_yield_ctr;

#ifdef __cplusplus
}
#endif

#endif // d_m3UsePsramMemory
#endif // m3_psram_glue_h
