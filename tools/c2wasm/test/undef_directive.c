/* Test: #undef removes a previously defined macro */
#include "conez_api.h"

#define FOO 42
#undef FOO
#define FOO 99

void setup(void) {
    /* FOO should be 99, not 42 */
    int x = FOO;
    print_i32(x);
}
