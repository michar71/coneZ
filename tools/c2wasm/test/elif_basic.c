/* Test: #elif support (fix #7) */
#include "conez_api.h"

#define MODE 2

void setup(void) {
#if MODE == 1
    print_i32(1);
#elif MODE == 2
    print_i32(2);
#elif MODE == 3
    print_i32(3);
#else
    print_i32(0);
#endif
}
