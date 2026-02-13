/*
 * bas2wasm_platform.c — embedded platform implementations
 * Only compiled when BAS2WASM_EMBEDDED is defined.
 */
#ifdef BAS2WASM_EMBEDDED

#include "bas2wasm.h"
#include <stdarg.h>

/* --- Callback state --- */
bw_diag_fn bw_on_error = NULL;
bw_diag_fn bw_on_info  = NULL;
void *bw_cb_ctx = NULL;
jmp_buf bw_bail;

/* --- Memory wrappers --- */
void *bw_malloc(size_t n)           { return malloc(n); }
void *bw_realloc(void *p, size_t n) { return realloc(p, n); }
void *bw_calloc(size_t n, size_t s) { return calloc(n, s); }
void  bw_free(void *p)              { free(p); }

/* --- PSRAM wrappers --- */
#ifdef BAS2WASM_USE_PSRAM
/* Forward-declare only the PSRAM functions we need (psram.h has C++ syntax) */
uint32_t psram_malloc(size_t size);
void     psram_free(uint32_t addr);
void     psram_read(uint32_t addr, uint8_t *buf, size_t len);
void     psram_write(uint32_t addr, const uint8_t *buf, size_t len);
uint32_t bw_psram_alloc(size_t size)                            { return psram_malloc(size); }
void     bw_psram_free(uint32_t addr)                           { psram_free(addr); }
void     bw_psram_read(uint32_t addr, void *dst, size_t len)    { psram_read(addr, (uint8_t *)dst, len); }
void     bw_psram_write(uint32_t addr, const void *src, size_t len) { psram_write(addr, (const uint8_t *)src, len); }
#endif

/* --- Diagnostics --- */
void bw_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    had_error = 1;
    if (bw_on_error) bw_on_error(buf, bw_cb_ctx);
}

void bw_info(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (bw_on_info) bw_on_info(buf, bw_cb_ctx);
}

void bw_fatal(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    had_error = 1;
    if (bw_on_error) bw_on_error(buf, bw_cb_ctx);
    longjmp(bw_bail, 1);
}

#endif /* BAS2WASM_EMBEDDED */
