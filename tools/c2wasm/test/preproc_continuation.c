/* Test: backslash line continuation in #define */
#include "conez_api.h"

#define LONG_VALUE 100 + \
    200 + \
    300

void setup(void) {
    int x = LONG_VALUE;
    print_i32(x);  /* 600 */
}

void loop(void) {
    delay_ms(1000);
}
