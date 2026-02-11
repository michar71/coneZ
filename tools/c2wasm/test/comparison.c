/* Test: comparison and logical operators, short-circuit */
#include "conez_api.h"

static int side_effect_counter = 0;

static int side_effect(void) {
    side_effect_counter++;
    return 1;
}

void setup(void) {
    /* Integer comparisons */
    print_i32(5 == 5);    /* 1 */
    print_i32(5 == 6);    /* 0 */
    print_i32(5 != 6);    /* 1 */
    print_i32(5 != 5);    /* 0 */
    print_i32(3 < 5);     /* 1 */
    print_i32(5 < 3);     /* 0 */
    print_i32(5 > 3);     /* 1 */
    print_i32(3 > 5);     /* 0 */
    print_i32(5 <= 5);    /* 1 */
    print_i32(5 <= 4);    /* 0 */
    print_i32(5 >= 5);    /* 1 */
    print_i32(4 >= 5);    /* 0 */

    /* Float comparisons */
    print_i32(1.5f == 1.5f);  /* 1 */
    print_i32(1.5f < 2.0f);   /* 1 */
    print_i32(2.0f > 1.5f);   /* 1 */

    /* Logical NOT */
    print_i32(!0);     /* 1 */
    print_i32(!1);     /* 0 */
    print_i32(!42);    /* 0 */

    /* Logical AND — short-circuit: right side should NOT execute */
    side_effect_counter = 0;
    int r1 = 0 && side_effect();
    print_i32(r1);                      /* 0 */
    print_i32(side_effect_counter);     /* 0 — not called */

    /* Logical AND — both sides evaluated */
    side_effect_counter = 0;
    int r2 = 1 && side_effect();
    print_i32(r2);                      /* 1 */
    print_i32(side_effect_counter);     /* 1 — called */

    /* Logical OR — short-circuit: right side should NOT execute */
    side_effect_counter = 0;
    int r3 = 1 || side_effect();
    print_i32(r3);                      /* 1 */
    print_i32(side_effect_counter);     /* 0 — not called */

    /* Logical OR — both sides evaluated */
    side_effect_counter = 0;
    int r4 = 0 || side_effect();
    print_i32(r4);                      /* 1 */
    print_i32(side_effect_counter);     /* 1 — called */
}

void loop(void) {
    delay_ms(1000);
}
