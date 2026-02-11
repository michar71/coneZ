/* Feature test: exercises many C language features */
#include "conez_api.h"

#define MAX_STEPS 16
#define CYCLE_MS  3000

static int counter = 0;
static float brightness = 0.0f;

static void ramp_up(int channel, int steps) {
    for (int i = 0; i < steps; i++) {
        int b = i * 16;
        led_fill(channel, b, b, b);
        led_show();
        delay_ms(20);
    }
}

static int clamp_val(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

void setup(void) {
    print("features test: starting\n");
    counter = 0;
    brightness = 1.0f;
}

void loop(void) {
    int phase = counter % 4;

    /* Test switch */
    switch (phase) {
        case 0:
            led_fill(1, 255, 0, 0);
            break;
        case 1:
            led_fill(1, 0, 255, 0);
            break;
        case 2:
            led_fill(1, 0, 0, 255);
            break;
        default:
            led_fill(1, 128, 128, 128);
            break;
    }
    led_show();

    /* Test for loop with bitwise ops */
    int n = led_count(1);
    for (int i = 0; i < n; i++) {
        int hue = ((i * 256 / n) + counter) & 0xFF;
        int sat = 255;
        int val = clamp_val((int)(brightness * 180.0f), 0, 255);
        led_set_pixel_hsv(1, i, hue, sat, val);
    }
    led_show();

    /* Test while loop */
    int x = 10;
    while (x > 0) {
        x--;
    }

    /* Test do/while */
    int y = 0;
    do {
        y++;
    } while (y < 5);

    /* Test compound assignment */
    counter += 1;
    brightness *= 0.99f;
    if (brightness < 0.1f) brightness = 1.0f;

    /* Test ternary */
    int bright = (counter > 100) ? 255 : counter * 2;
    (void)bright;

    /* Test logical operators */
    if (counter > 0 && counter < 1000) {
        /* Test pre-increment */
        int tmp = 0;
        ++tmp;
    }

    /* Test printf */
    printf("frame %d, brightness %.2f\n", counter, (double)brightness);

    delay_ms(30);
}
