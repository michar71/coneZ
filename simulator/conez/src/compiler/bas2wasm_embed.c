/*
 * bas2wasm_embed.c — single-TU wrapper for bas2wasm compiler in simulator
 *
 * All bas2wasm .c files are included here so that static variables and
 * functions remain internal.  Buf symbols are prefixed to bw_buf_* via
 * bas2wasm_platform.h to avoid collisions with c2wasm.
 *
 * Compiled as C (not C++) — symbols have C linkage by default.
 */
#define BAS2WASM_EMBEDDED

// Platform implementation (callbacks, memory wrappers)
#include "bas2wasm_platform.c"

// Compiler sources (order matters: buf before others that call buf_*)
#include "buf.c"
#include "imports.c"
#include "lexer.c"
#include "expr.c"
#include "stmt.c"
#include "assemble.c"

// main.c provides bw_compile(), bas2wasm_compile_buffer(), bas2wasm_reset()
#include "main.c"
