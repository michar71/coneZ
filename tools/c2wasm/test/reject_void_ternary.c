/* Negative test: ternary with void/non-void branches should error */
#include "conez_api.h"

void do_nothing(void) { }

void setup(void) {
    int x = 1 ? 42 : do_nothing();
}

void loop(void) {
    delay_ms(100);
}
