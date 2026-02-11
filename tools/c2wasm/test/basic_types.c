/* Test: basic types, globals, locals, initializers, const */
#include "conez_api.h"

#define MAGIC 42
#define HALF  0.5f

static int gi = 10;
static float gf = 3.14f;
int visible_global = 99;
static int neg_global = -7;

const int MAX_VAL = 255;

void setup(void) {
    /* Test int local with init */
    int a = 1;
    print_i32(a);

    /* Test float local with init */
    float b = 2.5f;
    print_f32(b);

    /* Test char type */
    char c = 'A';
    print_i32(c);

    /* Test unsigned/long qualifiers (accepted, treated as int) */
    unsigned int u = 100;
    long x = 200;
    long long ll = 300;
    print_i32(u);
    print_i32(x);
    print_i32(ll);

    /* Test global reads */
    print_i32(gi);
    print_f32(gf);
    print_i32(visible_global);
    print_i32(neg_global);

    /* Test #define macro expansion */
    print_i32(MAGIC);
    print_f32(HALF);

    /* Test const-as-define */
    print_i32(MAX_VAL);

    /* Test multiple declarations on one line */
    int p = 1, q = 2, r = 3;
    print_i32(p);
    print_i32(q);
    print_i32(r);

    /* Uninitialized local (should default to 0) */
    int z;
    (void)z;
}

void loop(void) {
    delay_ms(1000);
}
