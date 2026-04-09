/* Accessing a field that does not exist. */
#include "conez_api.h"

struct pt { int x; int y; };

void setup(void) {
    struct pt p;
    p.z = 1;
}
