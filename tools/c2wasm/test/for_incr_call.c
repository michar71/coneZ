/* Test: for-loop increment with function call (exercises call fixup adjustment) */
#include "conez_api.h"

void setup(void) {
    /* Increment expression contains a function call — fixups must be adjusted */
    int total = 0;
    for (int i = 0; i < 3; delay_ms(i++)) {
        total += 1;
    }
    print_i32(total);

    /* Condition + increment both have calls */
    for (int i = 0; i < led_count(1); delay_ms(1)) {
        if (i >= 3) break;
        i++;
    }
}
