/* Returning struct by value is not supported. */
#include "conez_api.h"

struct pt { int x; int y; };

struct pt make(void) {
    struct pt p;
    p.x = 1; p.y = 2;
    return p;
}

void setup(void) {
    struct pt r = make();
    print_i32(r.x);
}
