/* Test: #if defined() and #if !defined() */
#include "conez_api.h"

#define HAS_FEATURE 1

void setup(void) {
    /* #if defined(HAS_FEATURE) — should compile this block */
#if defined(HAS_FEATURE)
    print_i32(1);
#endif

    /* #if !defined(NO_FEATURE) — should compile this block */
#if !defined(NO_FEATURE)
    print_i32(2);
#endif

    /* #if defined(NO_FEATURE) — should NOT compile this block */
#if defined(NO_FEATURE)
    print_i32(999);
#endif

    /* #if !defined(HAS_FEATURE) — should NOT compile this block */
#if !defined(HAS_FEATURE)
    print_i32(999);
#endif

    /* #if defined without parens */
#if defined HAS_FEATURE
    print_i32(3);
#endif

    /* #if !defined without parens */
#if !defined NO_FEATURE
    print_i32(4);
#endif
}
