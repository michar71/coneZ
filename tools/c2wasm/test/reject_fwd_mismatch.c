/* Negative test: forward decl with mismatched params should error */
#include "conez_api.h"

int compute(int x, int y);

void setup(void) {
    int r = compute(10, 20);
    print_i32(r);
}

int compute(int x) {
    return x;
}

void loop(void) {
    delay_ms(100);
}
