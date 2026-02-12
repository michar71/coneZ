/* Test: additional character/string escape support */
#include "conez_api.h"

void setup(void) {
    int a = '\a';
    int b = '\b';
    int f = '\f';
    int v = '\v';
    int o = '\101';
    int q = '\?';
    const char *s = "A\101\n\t\v";

    print_i32(a);
    print_i32(b);
    print_i32(f);
    print_i32(v);
    print_i32(o);
    print_i32(q);
    print_i32((int)s[0]);
}

void loop(void) {
}
