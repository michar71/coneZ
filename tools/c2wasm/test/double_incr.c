/* Test: double pre/post increment/decrement */
#include "conez_api.h"

void setup(void) {
    double d = 1.0;
    ++d;      /* pre-increment: d = 2.0 */
    d++;      /* post-increment: d = 3.0 */
    --d;      /* pre-decrement: d = 2.0 */
    d--;      /* post-decrement: d = 1.0 */
    print_f64(d);
}

void loop(void) {}
