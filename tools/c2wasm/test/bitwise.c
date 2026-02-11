/* Test: bitwise operators, shifts */
#include "conez_api.h"

void setup(void) {
    /* AND */
    int a = 0xFF & 0x0F;   /* 0x0F = 15 */
    print_i32(a);

    /* OR */
    int b = 0xF0 | 0x0F;   /* 0xFF = 255 */
    print_i32(b);

    /* XOR */
    int c = 0xFF ^ 0x0F;   /* 0xF0 = 240 */
    print_i32(c);

    /* NOT (bitwise complement) */
    int d = ~0;             /* -1 */
    print_i32(d);

    /* Left shift */
    int e = 1 << 8;        /* 256 */
    print_i32(e);

    /* Right shift (signed) */
    int f = 256 >> 4;      /* 16 */
    print_i32(f);

    /* Combined: mask and shift */
    int rgb = 0xAABBCC;
    int r = (rgb >> 16) & 0xFF;   /* 0xAA = 170 */
    int g = (rgb >> 8) & 0xFF;    /* 0xBB = 187 */
    int blue = rgb & 0xFF;        /* 0xCC = 204 */
    print_i32(r);
    print_i32(g);
    print_i32(blue);

    /* Hex literals */
    int h1 = 0xFF;
    int h2 = 0x1A;
    print_i32(h1);
    print_i32(h2);

    /* Octal literal */
    int oct = 0755;
    print_i32(oct);
}

void loop(void) {
    delay_ms(1000);
}
