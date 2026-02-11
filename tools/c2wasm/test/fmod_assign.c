/* Test: %= with float, bitwise compound ops with float coercion */
#include "conez_api.h"

void setup(void) {
    /* Float %= should use fmodf */
    float x = 10.5f;
    x = fmodf(x, 3.0f);  /* direct fmodf call for comparison */
    print_f32(x);

    float y = 10.5f;
    int iy = (int)y;
    iy %= 3;
    print_i32(iy);

    /* Bitwise ops on int values (sanity check) */
    int a = 0xFF;
    a &= 0x0F;
    print_i32(a);

    int b = 0x0F;
    b |= 0xF0;
    print_i32(b);

    int c = 0xFF;
    c ^= 0x0F;
    print_i32(c);

    int d = 1;
    d <<= 4;
    print_i32(d);

    int e = 256;
    e >>= 4;
    print_i32(e);
}
