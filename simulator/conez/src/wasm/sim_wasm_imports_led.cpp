#include "sim_wasm_imports.h"
#include "led_state.h"
#include "sim_config.h"
#include "m3_env.h"

#include <cstring>
#include <algorithm>

// ---- Gamma table (same as firmware) ----

static bool wasm_use_gamma = false;

static const uint8_t gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255
};

static inline uint8_t wg(uint8_t v) { return wasm_use_gamma ? gamma8[v] : v; }

void wasm_reset_gamma() { wasm_use_gamma = false; }

// ---- HSV→RGB (FastLED rainbow algorithm) ----

static void hsv2rgb(uint8_t h, uint8_t s, uint8_t v, uint8_t &r, uint8_t &g, uint8_t &b)
{
    if (s == 0) { r = g = b = v; return; }

    uint8_t region = h / 43;
    uint8_t remainder = (h - region * 43) * 6;

    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:  r = v; g = t; b = p; break;
        case 1:  r = q; g = v; b = p; break;
        case 2:  r = p; g = v; b = t; break;
        case 3:  r = p; g = q; b = v; break;
        case 4:  r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
    }
}

static void rgb2hsv(uint8_t r, uint8_t g, uint8_t b, uint8_t &h, uint8_t &s, uint8_t &v)
{
    uint8_t mx = std::max({r, g, b});
    uint8_t mn = std::min({r, g, b});
    v = mx;
    if (mx == 0) { h = s = 0; return; }
    s = 255 * (int)(mx - mn) / mx;
    if (mx == mn) { h = 0; return; }
    int diff = mx - mn;
    if (mx == r)      h = 0   + 43 * (g - b) / diff;
    else if (mx == g) h = 85  + 43 * (b - r) / diff;
    else              h = 171 + 43 * (r - g) / diff;
}

// ---- Import functions ----

m3ApiRawFunction(m3_led_set_pixel) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, pos);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    ledState().setPixel(channel, pos, wg(r), wg(g), wg(b));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_fill) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    ledState().fill(channel, wg(r), wg(g), wg(b));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_show) {
    ledState().show();
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_count) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, channel);
    m3ApiReturn(ledState().count(channel));
}

m3ApiRawFunction(m3_led_set_pixel_hsv) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, pos);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);
    uint8_t r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    ledState().setPixel(channel, pos, wg(r), wg(g), wg(b));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_fill_hsv) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);
    uint8_t r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    ledState().fill(channel, wg(r), wg(g), wg(b));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_hsv_to_rgb) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);
    uint8_t r, g, b;
    hsv2rgb(h, s, v, r, g, b);
    m3ApiReturn(((int32_t)r << 16) | ((int32_t)g << 8) | (int32_t)b);
}

m3ApiRawFunction(m3_rgb_to_hsv) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    uint8_t h, s, v;
    rgb2hsv(r, g, b, h, s, v);
    m3ApiReturn(((int32_t)h << 16) | ((int32_t)s << 8) | (int32_t)v);
}

m3ApiRawFunction(m3_led_gamma8) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    m3ApiReturn((int32_t)gamma8[val]);
}

m3ApiRawFunction(m3_led_set_gamma) {
    m3ApiGetArg(int32_t, enable);
    wasm_use_gamma = (enable != 0);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_set_buffer) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, rgb_ptr);
    m3ApiGetArg(int32_t, count);

    if (count <= 0) { m3ApiSuccess(); }

    int max_count = ledState().count(channel);
    if (count > max_count) count = max_count;

    uint32_t mem_size = 0;
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || (uint32_t)rgb_ptr + (uint32_t)count * 3 > mem_size) {
        m3ApiTrap("led_set_buffer: out of bounds");
    }

    // Apply gamma if needed
    if (wasm_use_gamma) {
        std::vector<uint8_t> tmp(count * 3);
        const uint8_t *src = mem_base + rgb_ptr;
        for (int i = 0; i < count * 3; i++)
            tmp[i] = gamma8[src[i]];
        ledState().setBuffer(channel, tmp.data(), count);
    } else {
        ledState().setBuffer(channel, mem_base + rgb_ptr, count);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_shift) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, amount);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    ledState().shift(channel, amount, r, g, b);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_rotate) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, amount);
    ledState().rotate(channel, amount);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_led_reverse) {
    m3ApiGetArg(int32_t, channel);
    ledState().reverse(channel);
    m3ApiSuccess();
}

// ---- Link ----

M3Result link_led_imports(IM3Module module)
{
    M3Result r;

    r = m3_LinkRawFunction(module, "env", "led_set_pixel", "v(iiiii)", m3_led_set_pixel);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_fill", "v(iiii)", m3_led_fill);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_show", "v()", m3_led_show);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_count", "i(i)", m3_led_count);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "led_set_pixel_hsv", "v(iiiii)", m3_led_set_pixel_hsv);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_fill_hsv", "v(iiii)", m3_led_fill_hsv);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "hsv_to_rgb", "i(iii)", m3_hsv_to_rgb);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "rgb_to_hsv", "i(iii)", m3_rgb_to_hsv);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "led_gamma8", "i(i)", m3_led_gamma8);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_set_gamma", "v(i)", m3_led_set_gamma);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "led_set_buffer", "v(iii)", m3_led_set_buffer);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "led_shift", "v(iiiii)", m3_led_shift);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_rotate", "v(ii)", m3_led_rotate);
    if (r && r != m3Err_functionLookupFailed) return r;
    r = m3_LinkRawFunction(module, "env", "led_reverse", "v(i)", m3_led_reverse);
    if (r && r != m3Err_functionLookupFailed) return r;

    return m3Err_none;
}
