/* Test: int *a, *b; comma-separated pointer declarations (local + global).
 * Exercises that every `*name` in a comma list is an independent pointer
 * (correct type_info) and derefs/assigns through them. */
// EXPECTED:
// 7
// 7
// 11
// 22
#include <conez_api.h>

int g1 = 11, g2 = 22;
int *gp1, *gp2;

void setup(void) {
    int x = 7;
    int *a, *b;
    a = &x;
    b = a;
    gp1 = &g1;
    gp2 = &g2;
    print_i32(*a);    /* 7  */
    print_i32(*b);    /* 7  */
    print_i32(*gp1);  /* 11 */
    print_i32(*gp2);  /* 22 */
}

void loop(void) {}
