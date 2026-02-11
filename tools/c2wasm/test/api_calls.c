/* Test: ConeZ API import calls — LED, GPIO, sensors, time */
#include "conez_api.h"

void setup(void) {
    /* LED functions */
    led_fill(1, 255, 0, 0);
    led_show();
    int n = led_count(1);
    print_i32(n);

    led_set_pixel(1, 0, 128, 128, 128);
    led_set_pixel_hsv(1, 0, 0, 255, 255);
    led_fill_hsv(1, 128, 255, 200);
    led_show();

    /* Gamma */
    int g = led_gamma8(128);
    print_i32(g);

    /* Time functions */
    int ms = millis();
    print_i32(ms);

    /* Params */
    set_param(0, 42);
    int p = get_param(0);
    print_i32(p);

    /* Random */
    int r = random_int(1, 100);
    print_i32(r);

    /* GPS functions (just call to verify linking) */
    int gv = gps_valid();
    (void)gv;
    float lat = get_lat();
    (void)lat;

    /* Sensor functions */
    float temp = get_temp();
    (void)temp;
    float batt = get_bat_voltage();
    (void)batt;
}

void loop(void) {
    delay_ms(1000);
}
