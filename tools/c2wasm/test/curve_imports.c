/* Test: curve helper imports */
#include "conez_api.h"

void setup(void) {
    float lf = lerp(0.0f, 10.0f, 0.5f);
    int li = larp(0, 10, 20, 30, 0, 5, 10, 15);
    float lff = larpf(0.0f, 10.0f, 20.0f, 30.0f, 0.0f, 5.0f, 10.0f, 15);

    print_f32(lf);
    print_i32(li);
    print_f32(lff);
}

void loop(void) {
}
