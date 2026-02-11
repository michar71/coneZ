/* Test: long long global init (fix #2) */
#include "conez_api.h"

long long g1 = 42;
long long g2 = -100;
const long long G3 = 999;

void setup(void) {
    print_i64(g1);
    print_i64(g2);
    print_i64(G3);
}
