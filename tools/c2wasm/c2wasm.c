/*
 * c2wasm — C to WASM compiler for ConeZ
 *
 * Compiles a subset of C to WASM 1.0 binaries targeting the ConeZ host API.
 * No external dependencies beyond libc.
 *
 * Usage: cc -o c2wasm c2wasm.c && ./c2wasm input.c -o output.wasm
 */

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    fprintf(stderr, "c2wasm: not yet implemented\n");
    return 1;
}
