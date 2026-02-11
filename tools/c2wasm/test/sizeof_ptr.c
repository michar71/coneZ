/* Test: sizeof(type*) returns 4 (fix #23) */
#include "conez_api.h"

void setup(void) {
    print_i32(sizeof(int*));     /* 4 */
    print_i32(sizeof(float*));   /* 4 */
    print_i32(sizeof(char*));    /* 4 */
    print_i32(sizeof(void*));    /* 4 */
    print_i32(sizeof(double*));  /* 4 */

    /* Contrast with non-pointer */
    print_i32(sizeof(int));      /* 4 */
    print_i32(sizeof(double));   /* 8 */
    print_i32(sizeof(char));     /* 1 */
}

void loop(void) {}
