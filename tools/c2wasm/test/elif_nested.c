/* Test: nested #if inside #elif (fix #6/#7 combined) */
#include "conez_api.h"

#define A 0
#define B 1

void setup(void) {
    int r = 0;
#if A
    r = 1;
#elif B
    #ifdef UNDEFINED_THING
        r = 99;
    #else
        r = 2;
    #endif
#else
    r = 3;
#endif
    print_i32(r);  /* should be 2 */
}
