/**
 * hsv_rainbow.wasm — Smooth rainbow pattern using HSV color space.
 *
 * Sets each pixel on channel 1 to a different hue, then shifts
 * the offset each frame to animate the rainbow.
 *
 * Demonstrates: led_set_pixel_hsv, led_count, led_show, delay_ms.
 *
 * Build:
 *   cd tools/wasm
 *   make
 */

#include "conez_api.h"

static int offset = 0;

void setup(void) {
    print("hsv_rainbow: starting\n");
}

void loop(void) {
    int n = led_count(1);
    for (int i = 0; i < n; i++) {
        int hue = ((i * 256 / n) + offset) & 0xFF;
        led_set_pixel_hsv(1, i, hue, 255, 180);
    }
    led_show();
    offset = (offset + 2) & 0xFF;
    delay_ms(30);
}
