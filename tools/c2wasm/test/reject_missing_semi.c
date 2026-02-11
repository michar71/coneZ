/* Negative test: should fail — missing semicolon */
#include "conez_api.h"

void setup(void) {
    int x = 10
    print_i32(x);
}

void loop(void) {
    delay_ms(100);
}
