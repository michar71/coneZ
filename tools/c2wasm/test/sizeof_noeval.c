/* Test: sizeof(expr) doesn't evaluate the expression */
#include "conez_api.h"

static int counter = 0;

static int side_effect(void) {
    counter = counter + 1;  /* Would increment counter if evaluated */
    return 42;
}

void setup(void) {
    counter = 0;
    int sz = sizeof(side_effect());  /* Must NOT call side_effect */
    print_i32(sz);       /* 4 (sizeof int) */
    print_i32(counter);  /* 0 (side_effect not called) */

    int sf = sizeof(float);
    print_i32(sf);       /* 4 */

    int sd = sizeof(double);
    print_i32(sd);       /* 8 */
}
