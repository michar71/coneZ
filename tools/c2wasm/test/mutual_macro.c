/* Test: mutual macro recursion doesn't cause infinite loop */
#include "conez_api.h"

/* These mutually reference each other — must not infinite loop */
#define A B
#define B A

void setup(void) {
    /* A expands to B, B expands to A, depth limit hit → treated as identifier */
    int A = 10;
    int B = 20;
    print_i32(A);
    print_i32(B);
}
