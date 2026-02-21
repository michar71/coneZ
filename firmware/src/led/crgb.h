// Minimal CRGB/CHSV types — replaces FastLED.h for color math.
// Hardware output driven by RMT in led.cpp.

#ifndef CONEZ_CRGB_H
#define CONEZ_CRGB_H

#include <stdint.h>

struct CHSV {
    uint8_t h;  // hue        0-255
    uint8_t s;  // saturation 0-255
    uint8_t v;  // value      0-255

    CHSV() : h(0), s(0), v(0) {}
    CHSV(uint8_t hue, uint8_t sat, uint8_t val) : h(hue), s(sat), v(val) {}
};

struct CRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    CRGB() : r(0), g(0), b(0) {}
    CRGB(uint8_t red, uint8_t grn, uint8_t blu) : r(red), g(grn), b(blu) {}

    // Construct from packed 0xRRGGBB
    explicit CRGB(uint32_t color)
        : r((color >> 16) & 0xFF), g((color >> 8) & 0xFF), b(color & 0xFF) {}

    // Set from HSV (convenience wrapper)
    CRGB& setHSV(uint8_t hue, uint8_t sat, uint8_t val);

    // Assignment from packed 0xRRGGBB
    CRGB& operator=(uint32_t color) {
        r = (color >> 16) & 0xFF;
        g = (color >> 8) & 0xFF;
        b = color & 0xFF;
        return *this;
    }

    CRGB& operator+=(const CRGB& rhs) {
        // Saturating add
        uint16_t tr = r + rhs.r; r = (tr > 255) ? 255 : tr;
        uint16_t tg = g + rhs.g; g = (tg > 255) ? 255 : tg;
        uint16_t tb = b + rhs.b; b = (tb > 255) ? 255 : tb;
        return *this;
    }

    CRGB& operator|=(const CRGB& rhs) {
        if (rhs.r > r) r = rhs.r;
        if (rhs.g > g) g = rhs.g;
        if (rhs.b > b) b = rhs.b;
        return *this;
    }

    bool operator==(const CRGB& rhs) const {
        return r == rhs.r && g == rhs.g && b == rhs.b;
    }

    bool operator!=(const CRGB& rhs) const {
        return !(*this == rhs);
    }

    // Named constants
    static const CRGB Black;
    static const CRGB Red;
    static const CRGB Green;
    static const CRGB Blue;
    static const CRGB White;
};

// HSV → RGB (standard HSV, 0-255 ranges for all components)
void hsv2rgb_rainbow(const CHSV& hsv, CRGB& rgb);

// RGB → HSV (approximate, 0-255 ranges)
CHSV rgb2hsv_approximate(const CRGB& rgb);

#endif
