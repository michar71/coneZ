/* Test: statements before first case — compiles with warning */
// EXPECTED:
// 10
#include <conez_api.h>

void setup(void) {
    int x = 1;
    int result = 0;
    switch (x) {
        result = 99;  /* warning: before first case */
    case 1:
        result = 10;
        break;
    case 2:
        result = 20;
        break;
    }
    print_i32(result);
}

void loop(void) {
    delay_ms(1000);
}
