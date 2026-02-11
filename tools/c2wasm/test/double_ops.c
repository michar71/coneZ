/* Test: double (f64) type support — arithmetic, comparisons, globals, casts */
#include "conez_api.h"

double global_d = 3.14f;
static double offset = -1.0f;

static double add_doubles(double a, double b) {
    return a + b;
}

static double multiply(double x, int factor) {
    return x * (double)factor;
}

static int compare_doubles(double a, double b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

void setup(void) {
    /* Arithmetic */
    double x = 10.0f;
    double y = 3.0f;
    double sum = x + y;
    double diff = x - y;
    double prod = x * y;
    double quot = x / y;

    /* Mixed int/double */
    int n = 5;
    double mixed = x + (double)n;

    /* Function calls */
    double r = add_doubles(1.0f, 2.0f);
    double m = multiply(global_d, 10);

    /* Comparisons */
    int cmp = compare_doubles(x, y);
    int eq = (x == y);
    int ne = (x != y);
    int le = (x <= 10.0f);
    int ge = (x >= 10.0f);

    /* Compound assignment */
    double acc = 1.0f;
    acc += 2.0f;
    acc -= 0.5f;
    acc *= 3.0f;
    acc /= 2.0f;

    /* Cast to/from double */
    int i = (int)x;
    float f = (float)x;
    double d = (double)i;

    /* Use values to prevent dead-code elimination */
    (void)sum; (void)diff; (void)prod; (void)quot;
    (void)mixed; (void)r; (void)m; (void)cmp;
    (void)eq; (void)ne; (void)le; (void)ge;
    (void)acc; (void)i; (void)f; (void)d;
    (void)offset;
}
