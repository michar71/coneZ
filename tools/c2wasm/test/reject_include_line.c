/* Error after #include must report correct line number.
 * The #include is on line 6, struct_header.h is 10 lines.
 * Without line tracking, the error on line 10 would be reported as ~20.
 * With line tracking, it should be reported as line 10. */
#include "conez_api.h"
#include "struct_header.h"

void setup(void) {
    undefined_var = 1;
}
