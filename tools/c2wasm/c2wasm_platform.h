/*
 * c2wasm_platform.h — platform abstraction for standalone vs embedded use
 *
 * In standalone mode (default): all macros resolve to libc calls (zero overhead).
 * In embedded mode (C2WASM_EMBEDDED): function pointers + longjmp for fatal errors.
 */
#ifndef C2WASM_PLATFORM_H
#define C2WASM_PLATFORM_H

#include <stddef.h>
#include <setjmp.h>

#ifdef C2WASM_EMBEDDED

/* --- Diagnostic callbacks --- */
typedef void (*cw_diag_fn)(const char *msg, void *ctx);
extern cw_diag_fn cw_on_error;
extern cw_diag_fn cw_on_info;
extern void *cw_cb_ctx;
extern jmp_buf cw_bail;

/* --- Memory --- */
void *cw_malloc(size_t n);
void *cw_realloc(void *p, size_t n);
void *cw_calloc(size_t n, size_t sz);
void  cw_free(void *p);

/* --- Diagnostics --- */
void  cw_error(const char *fmt, ...);    /* sets had_error, calls on_error */
void  cw_info(const char *fmt, ...);     /* calls on_info */
void  cw_warn(const char *fmt, ...);     /* calls on_info (for #warning) */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#elif defined(_MSC_VER)
__declspec(noreturn)
#endif
void  cw_fatal(const char *fmt, ...);    /* calls on_error + longjmp */

/* --- Symbol prefixes to avoid link conflicts with bas2wasm --- */
#define buf_init       cw_buf_init
#define buf_grow       cw_buf_grow
#define buf_byte       cw_buf_byte
#define buf_bytes      cw_buf_bytes
#define buf_free       cw_buf_free
#define buf_uleb       cw_buf_uleb
#define buf_sleb       cw_buf_sleb
#define buf_sleb64     cw_buf_sleb64
#define buf_f32        cw_buf_f32
#define buf_f64        cw_buf_f64
#define buf_str        cw_buf_str
#define buf_section    cw_buf_section

/* Shared global/function names — prefix to avoid collisions */
#define source         cw_source
#define src_len        cw_src_len
#define src_pos        cw_src_pos
#define line_num       cw_line_num
#define had_error      cw_had_error
#define tok            cw_tok
#define data_buf       cw_data_buf
#define data_len       cw_data_len
#define cur_func       cw_cur_func
#define func_bufs      cw_func_bufs
#define nfuncs         cw_nfuncs
#define ctrl_stk       cw_ctrl_stk
#define ctrl_sp        cw_ctrl_sp
#define block_depth    cw_block_depth
#define imp_used       cw_imp_used
#define imp_defs       cw_imp_defs
#define expr           cw_expr
#define assemble_to_buf cw_assemble_to_buf
#define assemble       cw_assemble
#define syms           cw_syms
#define nsym           cw_nsym
#define cur_scope      cw_cur_scope
#define nglobals       cw_nglobals
#define has_setup      cw_has_setup
#define has_loop       cw_has_loop
#define type_had_pointer   cw_type_had_pointer
#define type_had_const     cw_type_had_const
#define type_had_unsigned  cw_type_had_unsigned
#define src_file       cw_src_file

#else /* standalone */

#include <stdio.h>
#include <stdlib.h>

#define cw_malloc   malloc
#define cw_realloc  realloc
#define cw_calloc   calloc
#define cw_free     free
#define cw_error(fmt, ...)  fprintf(stderr, fmt, ##__VA_ARGS__)
#define cw_info(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#define cw_warn(fmt, ...)   fprintf(stderr, fmt, ##__VA_ARGS__)
#define cw_fatal(fmt, ...)  do { fprintf(stderr, fmt, ##__VA_ARGS__); exit(1); } while(0)

#endif /* C2WASM_EMBEDDED */

#endif /* C2WASM_PLATFORM_H */
