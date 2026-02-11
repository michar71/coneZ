/* Test: additional WASM-native math builtins */
#include "conez_api.h"

void setup(void) {
    /* Single-arg builtins */
    float a = truncf(3.7f);
    double b = trunc(3.7);

    /* Two-arg builtins */
    float c = fminf(1.0f, 2.0f);
    float d = fmaxf(1.0f, 2.0f);
    double e = fmin(1.0, 2.0);
    double f = fmax(1.0, 2.0);

    /* With expressions */
    float g = fminf(a, truncf(5.5f));
    double h = fmax(b, trunc(-1.2));

    (void)c; (void)d; (void)e; (void)f; (void)g; (void)h;
}
