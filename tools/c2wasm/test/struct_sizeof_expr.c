/* sizeof(struct_variable) must return the actual struct size, not 4. */
#include "conez_api.h"

struct big { int a; int b; int c; int d; };

void setup(void) {
    struct big s;
    s.a = 1;
    print_i32(sizeof(s));              /* should be 16 */
    print_i32(sizeof(struct big));     /* should be 16 */
}
