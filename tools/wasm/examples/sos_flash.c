/**
 * sos_flash.wasm — Speed-of-sound synchronized flash effect.
 *
 * Port of the native SOS_effect2. Every 3 seconds, each cone flashes white
 * with a delay proportional to its GPS distance from the configured origin,
 * simulating a "speed of sound" wavefront expanding outward.
 *
 * Baseline glow: green if GPS has fix, blue if not.
 *
 * Build:
 *   cd tools/wasm
 *   clang --target=wasm32 -O2 -nostdlib \
 *     -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop \
 *     -Wl,--allow-undefined \
 *     -I . -o examples/sos_flash.wasm examples/sos_flash.c
 */

#include "conez_api.h"

#define MS_PER_CYCLE   3000
#define SOS_SPEED_SCALE 0.5f
#define SOS_MPS        (343.0f * SOS_SPEED_SCALE)

static int prev_sec = -1;

static void set_baseline(void) {
    if (gps_valid())
        led_fill(1, 0, 4, 0);   /* green — GPS OK */
    else
        led_fill(1, 0, 0, 10);  /* blue  — no fix */
    led_show();
}

void setup(void) {
    print("sos_flash: starting\n");
    set_baseline();
}

void loop(void) {
    int sec = get_second();

    /* Wait for the next 3-second boundary */
    if (sec == prev_sec || sec % 3 != 0) {
        delay_ms(10);
        return;
    }
    prev_sec = sec;

    /* Distance from origin in meters (0 if no fix or no origin) */
    float dist = origin_dist();
    printf("dist: %.1f m\n", (double)dist);

    /* Delay = distance / speed-of-sound, wrapped to cycle period */
    float offset_ms = fmodf(dist / SOS_MPS * 1000.0f, (float)MS_PER_CYCLE);
    printf("offset: %.1f ms\n", (double)offset_ms);

    delay_ms((int)(offset_ms + 0.5f));

    /* Ramp up: 16 steps, 0 → 240 brightness, 20 ms each */
    for (int step = 0; step < 16; step++) {
        int b = step * 16;
        led_fill(1, b, b, b);
        led_show();
        delay_ms(20);
    }

    /* Ramp down: 32 steps, 255 → 0 brightness, 20 ms each */
    for (int step = 0; step < 32; step++) {
        int b = 255 - step * 8;
        if (b < 0) b = 0;
        led_fill(1, b, b, b);
        led_show();
        delay_ms(20);
    }

    /* Return to baseline glow */
    set_baseline();
}
