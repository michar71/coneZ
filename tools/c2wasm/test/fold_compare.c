/* Test: comparison folds produce i32 0/1. Covers signed and unsigned
 * integer comparisons, i64, and f32. */

// EXPECTED:
// 1
// 0
// 1
// 0
// 1
// 1
// 1
// 0
#include <conez_api.h>

void setup(void) {
    print_i32(5 < 10);                 /* 1 */
    print_i32(10 < 5);                 /* 0 */
    print_i32(5 == 5);                 /* 1 */
    print_i32(5 != 5);                 /* 0 */
    print_i32(5 <= 5);                 /* 1 */
    print_i32(1000000LL > 999999LL);   /* 1 — i64 cmp fold */
    print_i32(1.5f < 2.5f);            /* 1 — f32 cmp fold */
    print_i32(2.0f > 3.0f);            /* 0 */
}

void loop(void) {}
