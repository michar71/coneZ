/* Negative test: should fail — wrong number of arguments to function */
#include "conez_api.h"

static int add(int a, int b) {
    return a + b;
}

void setup(void) {
    int r = add(1, 2, 3);
    print_i32(r);
}

void loop(void) {
    delay_ms(100);
}
