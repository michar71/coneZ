/* Test: ternary with float and double result types (fix #1) */
#include "conez_api.h"

void setup(void) {
    /* Float ternary */
    float a = 1 ? 1.5f : 2.5f;
    print_f32(a);                /* 1.5 */

    float b = 0 ? 1.5f : 2.5f;
    print_f32(b);                /* 2.5 */

    /* Double ternary */
    double c = 1 ? 1.5 : 2.5;
    print_f64(c);                /* 1.5 */

    double d = 0 ? 1.5 : 2.5;
    print_f64(d);                /* 2.5 */

    /* Mixed: int condition, float branches */
    int x = 5;
    float e = (x > 3) ? 10.0f : 20.0f;
    print_f32(e);                /* 10.0 */

    /* Nested float ternary */
    float f = (x < 3) ? 1.0f : (x < 7) ? 2.0f : 3.0f;
    print_f32(f);                /* 2.0 */

    /* Double ternary with comparison */
    double g = (x > 10) ? 100.0 : 200.0;
    print_f64(g);                /* 200.0 */
}

void loop(void) {}
