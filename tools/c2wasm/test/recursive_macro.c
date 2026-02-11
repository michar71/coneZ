/* Test that recursive macros don't cause infinite loop */
#include "conez_api.h"

/* These should not cause infinite recursion */
#define FOO FOO
#define BAR BAR

void setup(void) {
    /* FOO and BAR should pass through as identifiers after one expansion */
    int FOO = 42;
    int BAR = 99;
    print_i32(FOO);
    print_i32(BAR);
}
