/* Negative test: should fail — no setup() or loop() defined */
#include <conez_api.h>

static void helper(void) {
    print_i32(42);
}
