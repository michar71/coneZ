/* Test: 3-slot fold tracking collapses nested literal subexpressions
 * across an outer operator (was a 2-slot limitation before fold_p slot). */

// EXPECTED:
// 7
// 14
// 21
// 10
// 24
// 100
#include "conez_api.h"

void setup(void) {
    print_i32(1 + 2 * 3);              /* 7 — outer + must fold across inner * */
    print_i32(2 + 3 * 4);              /* 14 */
    print_i32((1 + 2) * (3 + 4));      /* 21 — both inner folds + outer fold */
    print_i32(1 + 2 + 3 + 4);          /* 10 — left-assoc chain */
    print_i32(2 * 3 * 4);              /* 24 */
    print_i32(10 * 10);                /* 100 — basic literal multiply */
}

void loop(void) {}
