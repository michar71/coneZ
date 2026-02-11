/* Test: nested #ifdef/#else doesn't leak (fix #6) */
#include "conez_api.h"

#define OUTER 1

void setup(void) {
    int x = 0;

#ifdef OUTER
    x = 1;
#ifdef INNER_UNDEF
    x = 99;
#else
    x = 2;
#endif
#else
    /* outer else — should be entirely skipped */
    x = 88;
#ifdef ANOTHER_UNDEF
    x = 77;
#else
    x = 66;
#endif
#endif

    print_i32(x);  /* should be 2 */
}
