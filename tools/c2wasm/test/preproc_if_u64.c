/* Test: #if unsigned 64-bit literal behavior */
#include "conez_api.h"

#if 18446744073709551615ULL > 0
int ull_gt_zero = 1;
#else
int ull_gt_zero = 0;
#endif

#if 18446744073709551615ULL > 9223372036854775807LL
int ull_gt_llmax = 1;
#else
int ull_gt_llmax = 0;
#endif

void setup(void) {
    print_i32(ull_gt_zero);
    print_i32(ull_gt_llmax);
}

void loop(void) {
}
