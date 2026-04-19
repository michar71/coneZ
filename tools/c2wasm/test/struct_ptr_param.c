/* Struct pointer as a function parameter — must correctly track struct_id
 * so that -> member access works inside the called function. */
#include "conez_api.h"

struct vec2 { int x; int y; };

void print_vec(struct vec2 *v) {
    print_i32(v->x);
    print_i32(v->y);
}

void offset_vec(struct vec2 *v, int dx, int dy) {
    v->x = v->x + dx;
    v->y = v->y + dy;
}

void setup(void) {
    struct vec2 p;
    p.x = 10;
    p.y = 20;
    print_vec(&p);
    offset_vec(&p, 5, 15);
    print_vec(&p);
}
