/* Test WASM-native math builtins (single opcode, no import) */
#include "conez_api.h"

void setup(void) {
    float x = 4.0f;
    float r = sqrtf(x);        /* f32.sqrt */
    float a = fabsf(-3.5f);    /* f32.abs */
    float f = floorf(2.7f);    /* f32.floor */
    float c = ceilf(2.3f);     /* f32.ceil */

    print_f32(r);
    print_f32(a);
    print_f32(f);
    print_f32(c);
}
