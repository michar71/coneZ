/* Test: unsigned long long macro initializer parsing */
#include "conez_api.h"

#define UMAX64 18446744073709551615ULL

unsigned long long g = UMAX64;

void setup(void) {
    print_i64((long long)g);
}

void loop(void) {
}
