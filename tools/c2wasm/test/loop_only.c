/* Test: program with only loop(), no setup() */
// EXPECTED:
// 1
#include <conez_api.h>

static int count = 0;

void loop(void) {
    count++;
    if (count > 5) return;
    print_i32(count);
    delay_ms(100);
}
