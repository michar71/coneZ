/* Test: user functions, forward declarations, recursion, return types */
#include "conez_api.h"

/* Forward declaration */
static int fib(int n);

/* Simple void function */
static void greet(void) {
    print("hello\n");
}

/* Function with return value */
static int square(int x) {
    return x * x;
}

/* Function with float args and return */
static float avg(float a, float b) {
    return (a + b) / 2.0f;
}

/* Multiple parameters */
static int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* Recursive function (forward-declared above) */
static int fib(int n) {
    if (n <= 1) return n;
    return fib(n - 1) + fib(n - 2);
}

/* Function calling other functions */
static int compute(int x) {
    int sq = square(x);
    int cl = clamp(sq, 0, 100);
    return cl;
}

void setup(void) {
    greet();

    print_i32(square(5));       /* 25 */
    print_i32(square(-3));      /* 9 */

    print_f32(avg(2.0f, 4.0f)); /* 3.0 */

    print_i32(clamp(50, 0, 100));   /* 50 */
    print_i32(clamp(-10, 0, 100));  /* 0 */
    print_i32(clamp(200, 0, 100));  /* 100 */

    print_i32(fib(0));   /* 0 */
    print_i32(fib(1));   /* 1 */
    print_i32(fib(7));   /* 13 */

    print_i32(compute(8));   /* clamp(64, 0, 100) = 64 */
    print_i32(compute(12));  /* clamp(144, 0, 100) = 100 */
}

void loop(void) {
    delay_ms(1000);
}
