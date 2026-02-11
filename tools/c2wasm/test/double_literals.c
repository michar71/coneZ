/* Test: unsuffixed float literals are double (f64), f-suffixed are float (f32) */
#include "conez_api.h"

void setup(void) {
    /* Unsuffixed = double (f64) — should preserve full precision */
    double pi = 3.14159265358979;
    double x = 1.0 + 2.0;
    double y = pi * 2.0;

    /* f-suffixed = float (f32) */
    float a = 3.14f;
    float b = 1.0f + 2.0f;

    /* Mixed: unsuffixed literal with float var → promoted */
    float c = 3.14;

    /* Trailing dot = double */
    double d = 5.;

    (void)x; (void)y; (void)a; (void)b; (void)c; (void)d;
}
