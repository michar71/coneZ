/* Negative test: should fail with undefined variable error */
#include "conez_api.h"

void setup(void) {
    print_i32(undefined_var);
}

void loop(void) {
    delay_ms(100);
}
