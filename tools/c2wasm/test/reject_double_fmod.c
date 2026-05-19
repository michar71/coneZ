/* reject: `%` / `%=` applied to floating-point operands.
 * C11 6.5.5 — the operands of `%` shall have integer type; clang errors.
 * c2wasm previously accepted this and silently emitted fmod(). */
#include <conez_api.h>

void setup(void) {
    double a = 10.5;
    double b = 3.0;
    double c = a % b;   /* error: floating operands to '%' */
    double d = 10.5;
    d %= 3.0;           /* error: floating operands to '%=' */
    print_f64(c);
    print_f64(d);
}

void loop(void) {}
