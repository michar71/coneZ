/* reject: `return;` with no expression in a value-returning function.
 * C11 6.8.6.4 — constraint violation; clang errors with -Wreturn-mismatch.
 * c2wasm previously accepted this and silently returned 0. */
#include <conez_api.h>

int always_zero(void) {
    return;   /* error: non-void function must return a value */
}

void setup(void) {
    (void)always_zero();
}

void loop(void) {}
