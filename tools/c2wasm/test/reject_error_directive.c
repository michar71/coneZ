/* Test: #error directive causes compile failure (fix #10) */
#include "conez_api.h"

#error this should fail

void setup(void) {
    print_i32(0);
}
