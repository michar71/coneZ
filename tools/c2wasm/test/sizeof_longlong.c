/* Test: sizeof(long long) returns 8 (fix #1) */
#include "conez_api.h"

void setup(void) {
    long long x = 0;
    int s1 = sizeof(long long);
    int s2 = sizeof(x);
    print_i32(s1);  /* should be 8 */
    print_i32(s2);  /* should be 8 */
}
