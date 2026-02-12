/* Test: const global long long literal path */

#include "conez_api.h"

const long long BIG = 2147483648;

void setup(void) {
    long long x = BIG;
    print_i64(x);
}

void loop(void) {
}
