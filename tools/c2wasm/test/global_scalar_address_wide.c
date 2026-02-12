/* Test: address-of global float/double/long long scalars */
#include "conez_api.h"

long long gll = 10;
float gf = 1.5f;
double gd = 2.0;

void setup(void) {
    long long *pll = &gll;
    float *pf = &gf;
    double *pd = &gd;

    *pll = *pll + 7;
    *pf = *pf + 0.25f;
    *pd = *pd * 2.0;

    print_i64(gll);
    print_f32(gf);
    print_f64(gd);
}

void loop(void) {
}
