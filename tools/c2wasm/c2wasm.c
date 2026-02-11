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
#include <string.h>

#define C2WASM_VERSION_MAJOR 0
#define C2WASM_VERSION_MINOR 1

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("c2wasm %d.%d.%04d\n", C2WASM_VERSION_MAJOR, C2WASM_VERSION_MINOR, BUILD_NUMBER);
            return 0;
        }
    }
    fprintf(stderr, "c2wasm: not yet implemented\n");
    return 1;
}
