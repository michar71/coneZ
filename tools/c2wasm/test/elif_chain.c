/* Test: #elif chain with first branch taken */
#include "conez_api.h"

#define X 1

void setup(void) {
    int result = 0;
#if X == 1
    result = 10;
#elif X == 2
    result = 20;
#elif X == 3
    result = 30;
#else
    result = 40;
#endif
    print_i32(result);  /* 10 */
}
