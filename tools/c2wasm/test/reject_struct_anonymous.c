/* Anonymous structs are not supported. */
#include "conez_api.h"

void setup(void) {
    struct { int x; } s;
    s.x = 1;
}
