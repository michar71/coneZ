/* Test: bare 'long' compiles with warning (fix #9) */
#include "conez_api.h"

long x = 42;

void setup(void) {
    long y = x + 1;
    print_i32(y);  /* 43 */
}
