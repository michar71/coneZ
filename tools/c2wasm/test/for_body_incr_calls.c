/* Test: for-loop with function calls in BOTH body and increment.
 * Exercises the call fixup sorting fix. */
#include "conez_api.h"

static int step(int x) {
    return x + 1;
}

void setup(void) {
    /* Both body and increment have function calls */
    int total = 0;
    for (int i = 0; i < 5; i = step(i)) {
        total += led_count(1);
    }
    print_i32(total);

    /* Nested: condition + body + increment all have calls */
    int sum = 0;
    for (int j = 0; j < led_count(1); j = step(j)) {
        sum += millis();
        delay_ms(1);
    }
    print_i32(sum);
}
