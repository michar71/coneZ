/* Test: const int with negative, char, and float initializers */
#include "conez_api.h"

const int NEG_ONE = -1;
const int NEG_TEN = -10;
const int CHAR_A = 'A';
const int ZERO = 0;
const float PI = 3.14f;
const float NEG_PI = -3.14f;

void setup(void) {
    int a = NEG_ONE;
    int b = NEG_TEN;
    int c = CHAR_A;
    int d = ZERO;
    float p = PI;
    float np = NEG_PI;

    print_i32(a);
    print_i32(b);
    print_i32(c);
    print_i32(d);
    print_f32(p);
    print_f32(np);
}
