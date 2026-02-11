/* Test: comma operator in expressions */
#include "conez_api.h"

void setup(void) {
    int a = 0;
    int b = 0;

    /* Comma in expression statement — evaluates both, result is last */
    a = 10, b = 20;
    print_i32(a);
    print_i32(b);

    /* Comma in for-loop init and increment */
    int x = 0;
    int y = 0;
    for (x = 0, y = 100; x < 3; x++, y--)
        ;
    print_i32(x);
    print_i32(y);
}
