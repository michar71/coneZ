/* Test: pointer comma-separated declarations (fix #5) */
#include "conez_api.h"

/* Global pointer + non-pointer in same declaration */
int *gp, gi;

void setup(void) {
    /* Local pointer + non-pointer */
    float *lp, lf;
    lf = 3.14f;
    print_f32(lf);               /* 3.14 */

    gi = 42;
    print_i32(gi);               /* 42 */

    /* Second variable should be non-pointer (plain int/float) */
    int *p2, val;
    val = 100;
    print_i32(val);              /* 100 */
}

void loop(void) {}
