/* Test: printf with long long args (fix #5) */
#include "conez_api.h"

void setup(void) {
    long long val = 123456;
    printf("value=%lld\n", val);
}
