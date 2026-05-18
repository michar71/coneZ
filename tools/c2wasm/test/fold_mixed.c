/* Test: mixed-type folds apply C's standard promotion rules. */

// EXPECTED:
// 3
// 3
// 7
// 1
// 1
#include <conez_api.h>

void setup(void) {
    print_f32(1 + 2.0f);               /* 3 — int promoted to f32 */
    print_i64((int)1 + 2LL);           /* 3 — int extended to i64 */
    print_f32(1.0f + 2.0f * 3.0f);     /* 7 (= 7.0) */
    print_i32(1U < 2U);                /* 1 — unsigned cmp */
    print_i32((unsigned)5 > 3);        /* 1 — unsigned vs signed */
}

void loop(void) {}
