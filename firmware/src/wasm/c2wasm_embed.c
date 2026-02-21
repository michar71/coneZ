/*
 * c2wasm_embed.c — single-TU wrapper for c2wasm compiler in firmware
 *
 * All c2wasm .c files are included here so that static variables and
 * functions remain internal.  Buf symbols are prefixed to cw_buf_* via
 * c2wasm_platform.h to avoid collisions with bas2wasm.
 *
 * Compiled as C (not C++) — symbols have C linkage by default.
 *
 * Note: c2wasm_all.c lives in tools/c2wasm/ so its #include directives
 * resolve to that directory first, avoiding ambiguity with identically-
 * named files in tools/bas2wasm/.
 */
#ifdef INCLUDE_C_COMPILER

#define C2WASM_EMBEDDED

// In embedded mode, symbol names are intentionally truncated to 32 bytes.
// Suppress -Wformat-truncation which warns about this safe, intended behavior.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"

// Platform implementation (callbacks, memory wrappers)
#include "c2wasm_platform.c"

// All compiler sources via directory-anchored wrapper
#include "c2wasm_all.c"

#pragma GCC diagnostic pop

#endif /* INCLUDE_C_COMPILER */
