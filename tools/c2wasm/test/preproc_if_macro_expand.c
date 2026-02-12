/* Test: #if macro expansion of identifiers/expressions */
#include "conez_api.h"

#define A B
#define B 1
#define C (2 + 3)
#define X Y
#define Y X

#if A
int a_ok = 1;
#else
int a_ok = 0;
#endif

#if C == 5
int c_ok = 1;
#else
int c_ok = 0;
#endif

/* Recursive macro cycle should not hang; treated as false */
#if X
int x_ok = 1;
#else
int x_ok = 0;
#endif

void setup(void) {
    print_i32(a_ok);
    print_i32(c_ok);
    print_i32(x_ok);
}

void loop(void) {
}
