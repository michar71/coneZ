/* Instantiating a forward-declared but incomplete struct must error. */
#include "conez_api.h"

struct foo;

void setup(void) {
    struct foo s;
}
