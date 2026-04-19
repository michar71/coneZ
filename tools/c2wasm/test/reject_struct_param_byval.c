/* Passing struct by value is not supported — must use a pointer. */
#include "conez_api.h"

struct pt { int x; int y; };

void foo(struct pt p) {
    print_i32(p.x);
}

void setup(void) {
    struct pt a;
    a.x = 1; a.y = 2;
    foo(a);
}
