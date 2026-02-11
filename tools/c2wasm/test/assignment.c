/* Test: assignment operators, increment/decrement */
#include "conez_api.h"

void setup(void) {
    /* Compound assignment — int */
    int a = 10;
    a += 5;     print_i32(a);    /* 15 */
    a -= 3;     print_i32(a);    /* 12 */
    a *= 2;     print_i32(a);    /* 24 */
    a /= 4;     print_i32(a);    /* 6 */
    a %= 4;     print_i32(a);    /* 2 */

    /* Compound assignment — bitwise */
    int b = 0xFF;
    b &= 0x0F;  print_i32(b);   /* 15 */
    b |= 0xF0;  print_i32(b);   /* 255 */
    b ^= 0xFF;  print_i32(b);   /* 0 */
    b = 1;
    b <<= 4;    print_i32(b);   /* 16 */
    b >>= 2;    print_i32(b);   /* 4 */

    /* Compound assignment — float */
    float f = 10.0f;
    f += 2.5f;   print_f32(f);  /* 12.5 */
    f -= 0.5f;   print_f32(f);  /* 12.0 */
    f *= 2.0f;   print_f32(f);  /* 24.0 */
    f /= 3.0f;   print_f32(f);  /* 8.0 */

    /* Pre-increment/decrement */
    int c = 5;
    int r1 = ++c;   /* c=6, r1=6 */
    print_i32(c);
    print_i32(r1);

    int r2 = --c;   /* c=5, r2=5 */
    print_i32(c);
    print_i32(r2);

    /* Post-increment/decrement */
    int d = 10;
    int r3 = d++;   /* r3=10, d=11 */
    print_i32(r3);
    print_i32(d);

    int r4 = d--;   /* r4=11, d=10 */
    print_i32(r4);
    print_i32(d);

    /* Pre-increment on float */
    float ff = 1.0f;
    ++ff;
    print_f32(ff);   /* 2.0 */
    --ff;
    print_f32(ff);   /* 1.0 */
}

void loop(void) {
    delay_ms(1000);
}
