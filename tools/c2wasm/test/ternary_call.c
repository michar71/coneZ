/* Test: ternary else-branch with function call (exercises call fixup adjustment) */
#include "conez_api.h"

void setup(void) {
    int x = 1;
    /* The else-branch calls millis() — this requires correct fixup adjustment */
    int result = (x > 0) ? 42 : millis();
    print_i32(result);

    /* Nested: both branches have function calls */
    int a = (x > 0) ? led_count(1) : millis();
    print_i32(a);
}
