/* Test: program with only setup(), no loop() */
// EXPECTED:
// setup only
//
// 42
#include <conez_api.h>

void setup(void) {
    print("setup only\n");
    print_i32(42);
}
