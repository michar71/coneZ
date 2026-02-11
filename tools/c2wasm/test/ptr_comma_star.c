/* Test: int *a, *b; comma-separated pointer declarations */
#include "conez_api.h"

int *gp1, *gp2;

void setup(void) {
    int *a, *b;
    a = 0;
    b = 0;
    gp1 = 100;
    gp2 = 200;
    print_i32(a);
    print_i32(b);
    print_i32(gp1);
    print_i32(gp2);
}

void loop(void) {
    delay_ms(1000);
}
