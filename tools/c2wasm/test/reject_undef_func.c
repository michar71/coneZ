/* Reject test: forward-declared but undefined function */
#include "conez_api.h"

int helper(int x);  /* declared but never defined */

void setup(void) {
    int r = helper(5);
    print_i32(r);
}
