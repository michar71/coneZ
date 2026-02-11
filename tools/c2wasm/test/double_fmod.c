/* Test: double % and %= operations */
#include "conez_api.h"

void setup(void) {
    double a = 10.5;
    double b = 3.0;
    double c = a % b;  /* binary % on doubles */
    print_f64(c);

    double d = 10.5;
    d %= 3.0;  /* compound %= on doubles */
    print_f64(d);
}

void loop(void) {}
