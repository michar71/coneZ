/* Test: #if with multi-digit numbers */
#include "conez_api.h"

#if 10
#define VALUE_A 1
#else
#define VALUE_A 0
#endif

#if 0
#define VALUE_B 0
#else
#define VALUE_B 1
#endif

#if 100
#define VALUE_C 1
#else
#define VALUE_C 0
#endif

void setup(void) {
    print_i32(VALUE_A);  /* 1 — #if 10 is truthy */
    print_i32(VALUE_B);  /* 1 — #if 0 is falsy, #else taken */
    print_i32(VALUE_C);  /* 1 — #if 100 is truthy */
}
