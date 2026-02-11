/* Test: forward declaration with matching definition */
#include "conez_api.h"

int compute(int x, int y);

void setup(void) {
    int r = compute(10, 20);
    print_i32(r);
}

int compute(int x, int y) {
    return x + y;
}

void loop(void) {
    delay_ms(1000);
}
