/* Negative test: should fail with undefined function error */
#include "conez_api.h"

void setup(void) {
    nonexistent_function(42);
}

void loop(void) {
    delay_ms(100);
}
