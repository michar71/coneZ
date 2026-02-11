/* Test: math import functions */
#include "conez_api.h"

void setup(void) {
    /* sinf / cosf */
    float s = sinf(0.0f);
    print_f32(s);               /* 0.0 */
    float c = cosf(0.0f);
    print_f32(c);               /* 1.0 */

    /* tanf */
    float t = tanf(0.0f);
    print_f32(t);               /* 0.0 */

    /* atan2f */
    float a = atan2f(1.0f, 1.0f);
    print_f32(a);               /* ~0.785 (pi/4) */

    /* powf */
    float p = powf(2.0f, 10.0f);
    print_f32(p);               /* 1024.0 */

    /* expf / logf */
    float e = expf(0.0f);
    print_f32(e);               /* 1.0 */
    float l = logf(1.0f);
    print_f32(l);               /* 0.0 */

    /* log2f */
    float l2 = log2f(8.0f);
    print_f32(l2);              /* 3.0 */

    /* fmodf */
    float fm = fmodf(7.5f, 3.0f);
    print_f32(fm);              /* 1.5 */
}

void loop(void) {
    delay_ms(1000);
}
