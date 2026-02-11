/* Test: #if !0 and #if !1 preprocessor (fix #4) */
#include "conez_api.h"

/* #if !0 should be true (include this block) */
#if !0
#define NOT_ZERO_WORKS 1
#endif

/* #if !1 should be false (skip this block) */
#if !1
#define NOT_ONE_WRONG 1
#endif

/* #if 1 normal case */
#if 1
#define NORMAL_ONE 1
#endif

/* #if 0 normal case */
#if 0
#define NORMAL_ZERO_WRONG 1
#endif

void setup(void) {
    print_i32(NOT_ZERO_WORKS);   /* 1 */
    print_i32(NORMAL_ONE);       /* 1 */

#ifdef NOT_ONE_WRONG
    print_i32(999);              /* should NOT print */
#endif

#ifdef NORMAL_ZERO_WRONG
    print_i32(888);              /* should NOT print */
#endif
}

void loop(void) {}
