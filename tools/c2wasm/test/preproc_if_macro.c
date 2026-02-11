/* Test: #if MACRO_NAME expansion */
#include "conez_api.h"

#define VERSION 3
#define ZERO 0

#if VERSION
int version_ok = 1;
#else
int version_ok = 0;
#endif

#if ZERO
int zero_true = 1;
#else
int zero_true = 0;
#endif

/* Undefined macro evaluates to 0 */
#if UNDEFINED_MACRO
int undef_true = 1;
#else
int undef_true = 0;
#endif

void setup(void) {
    print_i32(version_ok);  /* 1 */
    print_i32(zero_true);   /* 0 */
    print_i32(undef_true);  /* 0 */
}

void loop(void) {
    delay_ms(1000);
}
