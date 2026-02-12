/* Test: #if character escape parsing */
#include "conez_api.h"

#if '\101' == 'A'
int oct_ok = 1;
#else
int oct_ok = 0;
#endif

#if '\a' == 7
int bell_ok = 1;
#else
int bell_ok = 0;
#endif

void setup(void) {
    print_i32(oct_ok);
    print_i32(bell_ok);
}

void loop(void) {
}
