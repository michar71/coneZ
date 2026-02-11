/* Test: variable scoping — block scope, for-loop init scope, shadowing */
#include "conez_api.h"

void setup(void) {
    int x = 1;

    /* Inner block shadows outer */
    {
        int x = 2;
        print_i32(x);       /* 2 */
    }
    print_i32(x);           /* 1 — outer x unchanged */

    /* for-loop init scope */
    for (int i = 0; i < 3; i++) {
        print_i32(i);
    }
    /* i should not be visible here — but we can declare a new i */
    int i = 99;
    print_i32(i);           /* 99 */

    /* Nested for-loop scopes */
    int total = 0;
    for (int j = 0; j < 3; j++) {
        for (int j = 0; j < 2; j++) {
            total++;
        }
    }
    print_i32(total);       /* 6 */
}

void loop(void) {
    delay_ms(1000);
}
