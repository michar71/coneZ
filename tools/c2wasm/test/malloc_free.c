/* Test: host-backed malloc/free/calloc/realloc imports */
#include "conez_api.h"

void setup(void) {
    int *p = (int *)malloc(2 * (int)sizeof(int));
    if (p) {
        p[0] = 7;
        p[1] = 9;
        p = (int *)realloc(p, 4 * (int)sizeof(int));
        if (p) {
            p[2] = p[0] + p[1];
            p[3] = 1;
        }
    }

    int *q = (int *)calloc(4, (int)sizeof(int));
    if (q) {
        q[0] = 3;
        q[1] = 4;
        q[2] = q[0] * q[1];
    }

    if (p) free(p);
    if (q) free(q);
}

void loop(void) {
}
