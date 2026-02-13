/*
 * c2wasm_platform.c — embedded platform implementations
 * Only compiled when C2WASM_EMBEDDED is defined.
 */
#ifdef C2WASM_EMBEDDED

#include "c2wasm.h"
#include <stdarg.h>

/* --- Callback state --- */
cw_diag_fn cw_on_error = NULL;
cw_diag_fn cw_on_info  = NULL;
void *cw_cb_ctx = NULL;
jmp_buf cw_bail;

/* --- Memory wrappers --- */
void *cw_malloc(size_t n)           { return malloc(n); }
void *cw_realloc(void *p, size_t n) { return realloc(p, n); }
void *cw_calloc(size_t n, size_t s) { return calloc(n, s); }
void  cw_free(void *p)              { free(p); }

/* --- Diagnostics --- */
void cw_error(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    had_error = 1;
    if (cw_on_error) cw_on_error(buf, cw_cb_ctx);
}

void cw_info(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (cw_on_info) cw_on_info(buf, cw_cb_ctx);
}

void cw_warn(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (cw_on_info) cw_on_info(buf, cw_cb_ctx);
}

void cw_fatal(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    had_error = 1;
    if (cw_on_error) cw_on_error(buf, cw_cb_ctx);
    longjmp(cw_bail, 1);
}

#endif /* C2WASM_EMBEDDED */
