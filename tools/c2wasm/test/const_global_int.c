/* Test: int const and const int at global scope (fix #8) */
#include "conez_api.h"

const int A = 10;
int const B = 20;
const float PI = 3.14f;
const double E = 2.718;

void setup(void) {
    /* const int globals become compile-time macros */
    int x = A + B;
    print_i32(x);   /* 30 */
    print_f32(PI);
    print_f64(E);
}
