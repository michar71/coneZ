/*
 * c2wasm_embed.c — single-TU wrapper for c2wasm compiler in simulator
 *
 * All c2wasm .c files are included here so that static variables and
 * functions remain internal.  Buf symbols are prefixed to cw_buf_* via
 * c2wasm_platform.h to avoid collisions with bas2wasm.
 *
 * Compiled as C (not C++) — symbols have C linkage by default.
 */
#define C2WASM_EMBEDDED

// Platform implementation (callbacks, memory wrappers)
#include "c2wasm_platform.c"

// Compiler sources (order matters: buf before others that call buf_*)
#include "buf.c"
#include "imports.c"
#include "lexer.c"
#include "preproc.c"
#include "type.c"
#include "type_ops.c"
#include "expr.c"
#include "stmt.c"
#include "assemble.c"

// main.c provides cw_compile(), c2wasm_compile_buffer(), c2wasm_reset()
#include "main.c"
