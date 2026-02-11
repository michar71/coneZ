/* Test: preprocessor directives */

/* Standard header includes — silently ignored */
#include <stdint.h>

#include "conez_api.h"

/* Simple defines */
#define FOO 42
#define BAR 100
#define PI_VAL 3.14f

/* Redefine is allowed */
#define FOO 99

/* #ifdef / #ifndef / #else / #endif */
#define FEATURE_A

#ifdef FEATURE_A
#define RESULT_A 1
#else
#define RESULT_A 0
#endif

#ifndef FEATURE_B
#define RESULT_B 10
#else
#define RESULT_B 20
#endif

/* #if 0 block comment */
#if 0
This code is completely skipped by the preprocessor.
It can contain anything, even invalid C syntax: @#$%^
#endif

/* Nested ifdef */
#ifdef FEATURE_A
#ifdef FEATURE_B
#define NESTED_RESULT 0
#else
#define NESTED_RESULT 42
#endif
#endif

void setup(void) {
    /* Macro expansion */
    print_i32(FOO);              /* 99 (redefined) */
    print_i32(BAR);              /* 100 */

    /* Conditional results */
    print_i32(RESULT_A);         /* 1 */
    print_i32(RESULT_B);         /* 10 */
    print_i32(NESTED_RESULT);    /* 42 */
}

void loop(void) {
    delay_ms(1000);
}
