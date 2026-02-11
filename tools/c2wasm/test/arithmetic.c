/* Test: arithmetic operators, mixed int/float, precedence */
#include "conez_api.h"

void setup(void) {
    /* Basic int arithmetic */
    int a = 10 + 3;     /* 13 */
    int b = 10 - 3;     /* 7 */
    int c = 10 * 3;     /* 30 */
    int d = 10 / 3;     /* 3 */
    int e = 10 % 3;     /* 1 */
    print_i32(a);
    print_i32(b);
    print_i32(c);
    print_i32(d);
    print_i32(e);

    /* Float arithmetic */
    float fa = 1.5f + 2.5f;   /* 4.0 */
    float fb = 5.0f - 1.5f;   /* 3.5 */
    float fc = 2.0f * 3.0f;   /* 6.0 */
    float fd = 7.0f / 2.0f;   /* 3.5 */
    print_f32(fa);
    print_f32(fb);
    print_f32(fc);
    print_f32(fd);

    /* Mixed int/float promotion */
    float mixed = 3 + 1.5f;   /* 4.5 */
    print_f32(mixed);

    /* Operator precedence: * before + */
    int prec1 = 2 + 3 * 4;   /* 14, not 20 */
    print_i32(prec1);

    /* Parentheses override precedence */
    int prec2 = (2 + 3) * 4;  /* 20 */
    print_i32(prec2);

    /* Negation */
    int neg = -5;
    print_i32(neg);
    float fneg = -2.5f;
    print_f32(fneg);

    /* Unary plus */
    int pos = +5;
    print_i32(pos);
}

void loop(void) {
    delay_ms(1000);
}
