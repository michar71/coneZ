/* Test: ternary with long long produces correct block type (fix #4) */
#include "conez_api.h"

void setup(void) {
    int cond = 1;
    long long a = 100;
    long long b = 200;
    long long result = cond ? a : b;
    print_i64(result);
}
