/* Test: compound ops through pointer aliases on globals */
#include "conez_api.h"

int gi = 10;
long long gll = 20;
float gf = 1.0f;
double gd = 3.0;

void setup(void) {
    int *pi = &gi;
    int **ppi = &pi;
    long long *pll = &gll;
    float *pf = &gf;
    double *pd = &gd;

    **ppi += 5;
    *pll -= 4;
    *pf *= 2.5f;
    *pd /= 1.5;

    print_i32(gi);
    print_i64(gll);
    print_f32(gf);
    print_f64(gd);
}

void loop(void) {
}
