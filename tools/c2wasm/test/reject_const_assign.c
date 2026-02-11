/* Negative test: assignment to const local should error */
#include "conez_api.h"

void setup(void) {
    const int x = 42;
    x = 10;
}

void loop(void) {
    delay_ms(100);
}
