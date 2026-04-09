/* Struct pointer with -> access, malloc, field types. */
#include "conez_api.h"

struct rgba {
    int r;
    int g;
    int b;
    int a;
};

void setup(void) {
    struct rgba *p = (struct rgba *)malloc(sizeof(struct rgba));
    if (p == 0) return;
    p->r = 255;
    p->g = 128;
    p->b = 64;
    p->a = 32;
    print_i32(p->r + p->g + p->b + p->a);   /* 479 */
    free(p);
}
