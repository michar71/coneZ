/* reject: assigning a non-zero integer to a pointer without a cast.
 * C11 6.5.16.1 — only a null pointer constant (integer constant 0) may be
 * assigned to a pointer without a cast; clang errors with -Wint-conversion.
 * c2wasm previously accepted this silently. */
#include <conez_api.h>

int *gp;

void setup(void) {
    int *p;
    p = 0;        /* OK: null pointer constant */
    gp = 100;     /* error: incompatible integer to pointer conversion */
    (void)p;
}

void loop(void) {}
