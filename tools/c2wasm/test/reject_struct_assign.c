/* Struct-by-value assignment is not supported. */
#include "conez_api.h"

struct pt { int x; int y; };

void setup(void) {
    struct pt a;
    a.x = 1; a.y = 2;
    struct pt b;
    b = a;
}
