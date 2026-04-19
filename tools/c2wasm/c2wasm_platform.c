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
/* All allocation failures longjmp via cw_fatal so callers can use the result
 * without a NULL check. cw_free tolerates NULL. */
void *cw_malloc(size_t n) {
    void *p = malloc(n);
    if (!p) cw_fatal("out of memory (malloc %u)", (unsigned)n);
    return p;
}
void *cw_realloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q && n) cw_fatal("out of memory (realloc %u)", (unsigned)n);
    return q;
}
void *cw_calloc(size_t n, size_t s) {
    void *p = calloc(n, s);
    if (!p && n && s) cw_fatal("out of memory (calloc %u*%u)", (unsigned)n, (unsigned)s);
    return p;
}
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
