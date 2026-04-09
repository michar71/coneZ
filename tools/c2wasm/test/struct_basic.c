/* Basic struct support:
 *   - struct declaration in .c
 *   - struct value local
 *   - member access via '.'
 *   - sizeof(struct)
 */
#include "conez_api.h"

struct point {
    int x;
    int y;
};

void setup(void) {
    struct point p;
    p.x = 3;
    p.y = 4;
    print_i32(p.x);
    print_i32(p.y);
    print_i32(sizeof(struct point));
}
