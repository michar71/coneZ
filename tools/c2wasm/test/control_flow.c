/* Test: if/else, while, do/while, for, switch/case, break, continue */
#include "conez_api.h"

void setup(void) {
    /* if/else */
    int x = 10;
    if (x > 5) {
        print_i32(1);   /* taken */
    } else {
        print_i32(0);
    }

    if (x < 5) {
        print_i32(0);
    } else {
        print_i32(2);   /* taken */
    }

    /* if without else */
    if (x == 10)
        print_i32(3);

    /* Nested if/else */
    if (x > 20) {
        print_i32(0);
    } else if (x > 15) {
        print_i32(0);
    } else if (x > 5) {
        print_i32(4);   /* taken */
    } else {
        print_i32(0);
    }

    /* while loop */
    int count = 0;
    while (count < 5) {
        count++;
    }
    print_i32(count);   /* 5 */

    /* do/while — executes at least once */
    int dw = 0;
    do {
        dw++;
    } while (dw < 3);
    print_i32(dw);      /* 3 */

    /* do/while with false condition — still runs once */
    int once = 0;
    do {
        once = 99;
    } while (0);
    print_i32(once);    /* 99 */

    /* for loop */
    int sum = 0;
    for (int i = 1; i <= 10; i++) {
        sum += i;
    }
    print_i32(sum);     /* 55 */

    /* for loop with break */
    int bval = 0;
    for (int i = 0; i < 100; i++) {
        if (i == 7) break;
        bval = i;
    }
    print_i32(bval);    /* 6 */

    /* for loop with continue */
    int csum = 0;
    for (int i = 0; i < 10; i++) {
        if (i % 2 == 0) continue;
        csum += i;   /* sum of odd numbers 1+3+5+7+9 = 25 */
    }
    print_i32(csum);    /* 25 */

    /* while with break */
    int wb = 0;
    while (1) {
        if (wb >= 3) break;
        wb++;
    }
    print_i32(wb);      /* 3 */

    /* switch/case */
    int val = 2;
    switch (val) {
        case 0: print_i32(0); break;
        case 1: print_i32(0); break;
        case 2: print_i32(10); break;  /* taken */
        case 3: print_i32(0); break;
        default: print_i32(0); break;
    }

    /* switch with default */
    switch (99) {
        case 0: print_i32(0); break;
        case 1: print_i32(0); break;
        default: print_i32(11); break;  /* taken */
    }

    /* switch with negative case value */
    int neg = -1;
    switch (neg) {
        case -1: print_i32(12); break;  /* taken */
        case 0:  print_i32(0); break;
        case 1:  print_i32(0); break;
    }

    /* Empty for-loop body */
    int ef = 0;
    for (ef = 0; ef < 5; ef++) ;
    print_i32(ef);      /* 5 */

    /* Nested loops */
    int total = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            total++;
        }
    }
    print_i32(total);   /* 12 */
}

void loop(void) {
    delay_ms(1000);
}
