/* Test: unary minus on a literal folds (rewrites the slot in place,
 * preserves the outer fold predecessor so enclosing op can still fold). */

// EXPECTED:
// 2
// -5
// -6
// -1000
// 5
#include <conez_api.h>

void setup(void) {
    print_i32(-5 + 7);                 /* 2 — unary minus then add */
    print_i32(-(2 + 3));               /* -5 — group folds, then unary */
    print_i32(2 * -3);                 /* -6 — outer mul against negated literal */
    print_i64(-1000LL);                /* -1000 — i64 unary */
    print_i32(-(-5));                  /* 5 — double negation */
}

void loop(void) {}
