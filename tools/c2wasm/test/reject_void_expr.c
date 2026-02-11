/* Test: reject void expression used as value */
#include "conez_api.h"

void setup(void) {
    int x = 1 + led_show();
    (void)x;
}
