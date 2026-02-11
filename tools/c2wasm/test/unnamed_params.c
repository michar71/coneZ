/* Test: functions with unnamed parameters */
#include "conez_api.h"

int add(int a, int) {
    return a + 1;
}

void callback(int, int, int code) {
    print_i32(code);
}

void setup(void) {
    int r = add(41, 0);
    print_i32(r);
    callback(0, 0, 99);
}

void loop(void) {
    delay_ms(1000);
}
