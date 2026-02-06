/**
 * rgb_cycle.wasm — Simple RGB cycle test for ConeZ WASM runtime.
 *
 * Cycles channel 1 through red, green, blue every 500ms.
 * Demonstrates: LED fill, delay, print, stop check.
 *
 * Build:
 *   cd tools/wasm
 *   clang --target=wasm32 -O2 -nostdlib \
 *     -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop \
 *     -Wl,--allow-undefined \
 *     -I . -o examples/rgb_cycle.wasm examples/rgb_cycle.c
 */

#include "conez_api.h"

static int frame = 0;

void setup(void) {
    print("rgb_cycle: starting\n");
}

void loop(void) {
    int phase = frame % 3;

    switch (phase) {
        case 0: led_fill(1, 255,   0,   0); break;  /* red   */
        case 1: led_fill(1,   0, 255,   0); break;  /* green */
        case 2: led_fill(1,   0,   0, 255); break;  /* blue  */
    }
    led_show();

    print_i32(frame);
    print(" ");

    frame++;
    delay_ms(500);
}
