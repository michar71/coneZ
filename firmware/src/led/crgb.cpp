#include "crgb.h"

// Named color constants
const CRGB CRGB::Black  = CRGB(0, 0, 0);
const CRGB CRGB::Red    = CRGB(255, 0, 0);
const CRGB CRGB::Green  = CRGB(0, 255, 0);
const CRGB CRGB::Blue   = CRGB(0, 0, 255);
const CRGB CRGB::White  = CRGB(255, 255, 255);


// HSV to RGB conversion (standard algorithm, 0-255 ranges).
// Hue: 0=red, 43=yellow, 85=green, 128=cyan, 171=blue, 213=magenta
void hsv2rgb_rainbow(const CHSV& hsv, CRGB& rgb)
{
    if (hsv.s == 0) {
        rgb.r = rgb.g = rgb.b = hsv.v;
        return;
    }

    uint8_t region = hsv.h / 43;
    uint8_t remainder = (hsv.h - region * 43) * 6;  // 0-255 within region

    uint8_t p = (uint8_t)((uint16_t)hsv.v * (255 - hsv.s) >> 8);
    uint8_t q = (uint8_t)((uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * remainder >> 8)) >> 8);
    uint8_t t = (uint8_t)((uint16_t)hsv.v * (255 - ((uint16_t)hsv.s * (255 - remainder) >> 8)) >> 8);

    switch (region) {
        case 0:  rgb.r = hsv.v; rgb.g = t;     rgb.b = p;     break;
        case 1:  rgb.r = q;     rgb.g = hsv.v; rgb.b = p;     break;
        case 2:  rgb.r = p;     rgb.g = hsv.v; rgb.b = t;     break;
        case 3:  rgb.r = p;     rgb.g = q;     rgb.b = hsv.v; break;
        case 4:  rgb.r = t;     rgb.g = p;     rgb.b = hsv.v; break;
        default: rgb.r = hsv.v; rgb.g = p;     rgb.b = q;     break;
    }
}


// RGB to HSV (approximate, 0-255 ranges)
CHSV rgb2hsv_approximate(const CRGB& rgb)
{
    uint8_t r = rgb.r, g = rgb.g, b = rgb.b;
    uint8_t mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    uint8_t delta = mx - mn;

    CHSV hsv;
    hsv.v = mx;

    if (delta == 0) {
        hsv.h = 0;
        hsv.s = 0;
        return hsv;
    }

    hsv.s = (uint8_t)((uint16_t)255 * delta / mx);

    int32_t hue;
    if (r == mx)
        hue = (int32_t)43 * (g - b) / delta;
    else if (g == mx)
        hue = 85 + (int32_t)43 * (b - r) / delta;
    else
        hue = 171 + (int32_t)43 * (r - g) / delta;

    if (hue < 0) hue += 256;
    hsv.h = (uint8_t)hue;
    return hsv;
}


CRGB& CRGB::setHSV(uint8_t hue, uint8_t sat, uint8_t val)
{
    CHSV hsv(hue, sat, val);
    hsv2rgb_rainbow(hsv, *this);
    return *this;
}
