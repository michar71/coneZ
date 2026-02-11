/* Test: ternary operator */
#include "conez_api.h"

void setup(void) {
    /* Basic ternary */
    int a = 1 ? 10 : 20;
    print_i32(a);               /* 10 */

    int b = 0 ? 10 : 20;
    print_i32(b);               /* 20 */

    /* Ternary with comparison */
    int x = 5;
    int c = (x > 3) ? 100 : 200;
    print_i32(c);               /* 100 */

    int d = (x > 10) ? 100 : 200;
    print_i32(d);               /* 200 */

    /* Ternary in expression */
    int e = 1 + ((x == 5) ? 10 : 0);
    print_i32(e);               /* 11 */

    /* Nested ternary */
    int f = (x < 3) ? 1 : (x < 7) ? 2 : 3;
    print_i32(f);               /* 2 */
}

void loop(void) {
    delay_ms(1000);
}
