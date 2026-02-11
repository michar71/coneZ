/* Test: type casts, sizeof, type coercion */
#include "conez_api.h"

void setup(void) {
    /* Cast int to float */
    int a = 7;
    float fa = (float)a;
    print_f32(fa);               /* 7.0 */

    /* Cast float to int (truncation) */
    float b = 3.9f;
    int ib = (int)b;
    print_i32(ib);               /* 3 */

    /* Double promotion and demotion */
    float c = 1.5f;
    int id = (int)(double)c;
    print_i32(id);               /* 1 */

    /* sizeof */
    print_i32(sizeof(int));      /* 4 */
    print_i32(sizeof(float));    /* 4 */
    print_i32(sizeof(char));     /* 1 */
    print_i32(sizeof(double));   /* 8 */

    /* Implicit coercion: int assigned to float var */
    float f = 42;
    print_f32(f);                /* 42.0 */

    /* void cast to suppress unused */
    int unused = 99;
    (void)unused;
}

void loop(void) {
    delay_ms(1000);
}
