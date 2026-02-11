/* Test: 'long long int' parses as CT_LONG_LONG (fix #3) */
#include "conez_api.h"

long long int x = 10;
unsigned long long int y = 20;

void setup(void) {
    int s = sizeof(long long int);
    print_i32(s);  /* should be 8 */
    print_i64(x);
    print_i64(y);
}
