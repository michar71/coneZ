/* Test inverse trig and origin imports */
// EXPECTED:
// 0.523599
// 1.047198
// 0.785398
// 0
// 0
#include <conez_api.h>

void setup(void) {
    float a = asinf(0.5f);
    float b = acosf(0.5f);
    float c = atanf(1.0f);
    print_f32(a);
    print_f32(b);
    print_f32(c);

    float lat = get_origin_lat();
    float lon = get_origin_lon();
    print_f32(lat);
    print_f32(lon);
}
