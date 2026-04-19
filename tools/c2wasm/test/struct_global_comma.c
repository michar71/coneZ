/* Comma-separated global struct declarations. Both variables must work. */
#include "conez_api.h"

struct pt { int x; int y; };

struct pt a, b;

void setup(void) {
    a.x = 1; a.y = 2;
    b.x = 3; b.y = 4;
    print_i32(a.x + a.y + b.x + b.y);  /* 10 */
}
