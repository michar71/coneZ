/*
 * c2wasm_all.c — includes all c2wasm source files for single-TU embedded builds
 *
 * This file must live in tools/c2wasm/ so that #include "foo.c" resolves
 * to this directory first (C include search: same directory as including file
 * before -I paths).  This prevents ambiguity with identically-named files
 * in tools/bas2wasm/ when both compilers are embedded in the same build.
 */
#include "buf.c"
#include "imports.c"
#include "lexer.c"
#include "preproc.c"
#include "type.c"
#include "type_ops.c"
#include "expr.c"
#include "stmt.c"
#include "assemble.c"
#include "main.c"
