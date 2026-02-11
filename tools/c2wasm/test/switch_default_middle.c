/* Test: default label can appear anywhere in switch */
#include "conez_api.h"

#define VAL_A 10
#define VAL_B 20

void setup(void) {
    /* 1. Default in middle, no case matches → default executes */
    int x = 99;
    switch (x) {
    case 1:
        print_i32(100);
        break;
    default:
        print_i32(200);    /* should print */
        break;
    case 2:
        print_i32(300);
        break;
    }

    /* 2. Default in middle, later case matches → specific case, not default */
    int y = 2;
    switch (y) {
    case 1:
        print_i32(400);
        break;
    default:
        print_i32(500);    /* should NOT print */
        break;
    case 2:
        print_i32(600);    /* should print */
        break;
    }

    /* 3. Fall-through from case into default */
    int z = 1;
    switch (z) {
    case 1:
        print_i32(700);    /* should print */
        /* no break — fall through into default */
    default:
        print_i32(800);    /* should print (fall-through) */
        break;
    case 2:
        print_i32(900);
        break;
    }

    /* 4. Fall-through from default into case */
    int w = 99;
    switch (w) {
    default:
        print_i32(1000);   /* should print (no match → default) */
        /* no break — fall through into case 1 */
    case 1:
        print_i32(1100);   /* should print (fall-through) */
        break;
    case 2:
        print_i32(1200);
        break;
    }

    /* 5. Default first in switch */
    int v = 3;
    switch (v) {
    default:
        print_i32(1300);   /* should NOT print (case 3 matches) */
        break;
    case 1:
        print_i32(1400);
        break;
    case 3:
        print_i32(1500);   /* should print */
        break;
    }

    /* 6. Default with macro case values */
    int m = 99;
    switch (m) {
    case VAL_A:
        print_i32(1600);
        break;
    default:
        print_i32(1700);   /* should print */
        break;
    case VAL_B:
        print_i32(1800);
        break;
    }

    /* 7. Default in middle, earlier case matches → specific case, not default */
    int u = 1;
    switch (u) {
    case 1:
        print_i32(1900);   /* should print */
        break;
    default:
        print_i32(2000);   /* should NOT print */
        break;
    case 2:
        print_i32(2100);
        break;
    }
}

void loop(void) {
    delay_ms(1000);
}
