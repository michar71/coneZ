/* Redefinition of the same struct tag is an error. */
#include "conez_api.h"

struct pt { int x; int y; };
struct pt { int x; int y; };

void setup(void) {
    struct pt p;
    p.x = 1;
}
