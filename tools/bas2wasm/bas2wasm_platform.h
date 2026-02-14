/*
 * bas2wasm_platform.h — platform abstraction for standalone vs embedded use
 *
 * In standalone mode (default): all macros resolve to libc calls (zero overhead).
 * In embedded mode (BAS2WASM_EMBEDDED): function pointers + longjmp for fatal errors.
 */
#ifndef BAS2WASM_PLATFORM_H
#define BAS2WASM_PLATFORM_H

#include <stddef.h>
#include <setjmp.h>

/* --- Diagnostic callbacks (always visible for API consumers) --- */
typedef void (*bw_diag_fn)(const char *msg, void *ctx);

#ifdef BAS2WASM_EMBEDDED

extern bw_diag_fn bw_on_error;
extern bw_diag_fn bw_on_info;
extern void *bw_cb_ctx;
extern jmp_buf bw_bail;

/* --- Memory --- */
void *bw_malloc(size_t n);
void *bw_realloc(void *p, size_t n);
void *bw_calloc(size_t n, size_t sz);
void  bw_free(void *p);

/* --- Diagnostics --- */
void  bw_error(const char *fmt, ...);    /* sets had_error, calls on_error */
void  bw_info(const char *fmt, ...);     /* calls on_info */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#elif defined(_MSC_VER)
__declspec(noreturn)
#endif
void  bw_fatal(const char *fmt, ...);    /* calls on_error + longjmp */

/* --- PSRAM wrappers (firmware only, for data_buf / data_items) --- */
#ifdef BAS2WASM_USE_PSRAM
uint32_t bw_psram_alloc(size_t size);
void     bw_psram_free(uint32_t addr);
void     bw_psram_read(uint32_t addr, void *dst, size_t len);
void     bw_psram_write(uint32_t addr, const void *src, size_t len);
#endif

/* --- Symbol prefixes to avoid link conflicts with c2wasm --- */
#define buf_init       bw_buf_init
#define buf_grow       bw_buf_grow
#define buf_byte       bw_buf_byte
#define buf_bytes      bw_buf_bytes
#define buf_free       bw_buf_free
#define buf_uleb       bw_buf_uleb
#define buf_sleb       bw_buf_sleb
#define buf_sleb64     bw_buf_sleb64
#define buf_f32        bw_buf_f32
#define buf_str        bw_buf_str
#define buf_section    bw_buf_section

/* Shared global/function names — prefix to avoid collisions */
#define source         bw_source
#define src_len        bw_src_len
#define src_pos        bw_src_pos
#define line_num       bw_line_num
#define had_error      bw_had_error
#define tok            bw_tok
#define vars           bw_vars
#define nvar           bw_nvar
#define data_buf       bw_data_buf
#define data_len       bw_data_len
#define data_items     bw_data_items
#define ndata_items    bw_ndata_items
#define option_base    bw_option_base
#define cur_func       bw_cur_func
#define func_bufs      bw_func_bufs
#define nfuncs         bw_nfuncs
#define ctrl_stk       bw_ctrl_stk
#define ctrl_sp        bw_ctrl_sp
#define block_depth    bw_block_depth
#define imp_used       bw_imp_used
#define imp_defs       bw_imp_defs
#define expr           bw_expr
#define assemble_to_buf bw_assemble_to_buf
#define assemble       bw_assemble

#else /* standalone */

#include <stdio.h>
#include <stdlib.h>

#define bw_malloc   malloc
#define bw_realloc  realloc
#define bw_calloc   calloc
#define bw_free     free
#define bw_error(fmt, ...)  fprintf(stderr, fmt, ##__VA_ARGS__)
#define bw_info(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#define bw_fatal(fmt, ...)  do { fprintf(stderr, fmt, ##__VA_ARGS__); exit(1); } while(0)

#endif /* BAS2WASM_EMBEDDED */

#endif /* BAS2WASM_PLATFORM_H */
