/* Test: program with only setup(), no loop() */
#include "conez_api.h"

void setup(void) {
    print("setup only\n");
    print_i32(42);
}
