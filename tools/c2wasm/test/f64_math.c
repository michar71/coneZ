/* Test: f64 (double) math import calls (fix #19) */
#include "conez_api.h"

void setup(void) {
    /* Basic trig */
    double s = sin(0.0);
    print_f64(s);                /* 0.0 */

    double c = cos(0.0);
    print_f64(c);                /* 1.0 */

    double t = tan(0.0);
    print_f64(t);                /* 0.0 */

    /* Inverse trig */
    double as = asin(0.0);
    print_f64(as);               /* 0.0 */

    double ac = acos(1.0);
    print_f64(ac);               /* 0.0 */

    double at = atan(0.0);
    print_f64(at);               /* 0.0 */

    /* Two-arg functions */
    double a2 = atan2(0.0, 1.0);
    print_f64(a2);               /* 0.0 */

    double p = pow(2.0, 10.0);
    print_f64(p);                /* 1024.0 */

    double fm = fmod(10.5, 3.0);
    print_f64(fm);               /* 1.5 */

    /* Exp/log */
    double e = exp(0.0);
    print_f64(e);                /* 1.0 */

    double l = log(1.0);
    print_f64(l);                /* 0.0 */

    double l2 = log2(8.0);
    print_f64(l2);               /* 3.0 */
}

void loop(void) {}
