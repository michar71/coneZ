/* Test: #if negative macro values */
#include "conez_api.h"

#define NEG -1
#define NEG2 (-2)

#if NEG < 0
int neg_lt_zero = 1;
#else
int neg_lt_zero = 0;
#endif

#if NEG2 < NEG
int neg_order = 1;
#else
int neg_order = 0;
#endif

void setup(void) {
    print_i32(neg_lt_zero);
    print_i32(neg_order);
}

void loop(void) {
}
