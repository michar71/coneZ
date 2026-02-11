/* Test: bare return in non-void function (fix #6) */
#include "conez_api.h"

int always_zero(void) {
    return;  /* bare return in non-void — should push 0 */
}

float always_zerof(void) {
    return;  /* bare return in float func — should push 0.0f */
}

void setup(void) {
    int a = always_zero();
    print_i32(a);                /* 0 */

    float b = always_zerof();
    print_f32(b);                /* 0.0 */
}

void loop(void) {}
