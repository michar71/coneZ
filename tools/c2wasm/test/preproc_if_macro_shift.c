/* Test: #if macro expressions with shifts and unary minus */
#include "conez_api.h"

#define HI (1ULL << 63)
#define NEG_U64 (-1ULL)

#if HI > 0
int hi_gt_zero = 1;
#else
int hi_gt_zero = 0;
#endif

#if HI < 0
int hi_lt_zero = 1;
#else
int hi_lt_zero = 0;
#endif

#if NEG_U64 > 0
int neg_u64_gt_zero = 1;
#else
int neg_u64_gt_zero = 0;
#endif

void setup(void) {
    print_i32(hi_gt_zero);
    print_i32(hi_lt_zero);
    print_i32(neg_u64_gt_zero);
}

void loop(void) {
}
