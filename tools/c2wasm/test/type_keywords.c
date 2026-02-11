/* Test: short, signed, _Bool type keywords */
#include "conez_api.h"

void setup(void) {
    short x = 42;
    signed int y = -10;
    _Bool b = 1;
    bool c = 0;
    signed char d = 65;

    print_i32(x);
    print_i32(y);
    print_i32(b);
    print_i32(c);
    print_i32(d);
}

void loop(void) {
    delay_ms(1000);
}
