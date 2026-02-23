#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "led.h"
#include "config.h"
#include <string.h>

// ---------- Auto-gamma state ----------
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

static inline uint8_t wasm_gamma(uint8_t v) {
    return wasm_use_gamma ? gamma8[v] : v;
}

static CRGB *led_buf_for_channel(int ch, int *count_out) {
    switch (ch) {
        case 1: *count_out = config.led_count1; return leds1;
        case 2: *count_out = config.led_count2; return leds2;
        case 3: *count_out = config.led_count3; return leds3;
        case 4: *count_out = config.led_count4; return leds4;
        default: *count_out = 0; return NULL;
    }
}

void wasm_reset_gamma(void) {
    wasm_use_gamma = false;
}

// --- LED core ---

// void led_set_pixel(i32 channel, i32 pos, i32 r, i32 g, i32 b)
m3ApiRawFunction(m3_led_set_pixel)
{
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, pos);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);

    CRGB *buf = NULL;
    int count = 0;
    switch (channel) {
        case 1: buf = leds1; count = config.led_count1; break;
        case 2: buf = leds2; count = config.led_count2; break;
        case 3: buf = leds3; count = config.led_count3; break;
        case 4: buf = leds4; count = config.led_count4; break;
    }
    if (buf && pos >= 0 && pos < count) {
        buf[pos] = CRGB(wasm_gamma((uint8_t)r), wasm_gamma((uint8_t)g), wasm_gamma((uint8_t)b));
    }

    m3ApiSuccess();
}

// void led_fill(i32 channel, i32 r, i32 g, i32 b)
m3ApiRawFunction(m3_led_fill)
{
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);

    CRGB col(wasm_gamma((uint8_t)r), wasm_gamma((uint8_t)g), wasm_gamma((uint8_t)b));
    if (channel >= 1 && channel <= 4) {
        int cnt = 0;
        switch (channel) {
            case 1: cnt = config.led_count1; break;
            case 2: cnt = config.led_count2; break;
            case 3: cnt = config.led_count3; break;
            case 4: cnt = config.led_count4; break;
        }
        led_set_channel(channel, cnt, col);
    }

    m3ApiSuccess();
}

// void led_show()
m3ApiRawFunction(m3_led_show)
{
    led_show();
    m3ApiSuccess();
}

// i32 led_count(i32 channel) -> number of LEDs on that channel
m3ApiRawFunction(m3_led_count)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, channel);

    int cnt = 0;
    switch (channel) {
        case 1: cnt = config.led_count1; break;
        case 2: cnt = config.led_count2; break;
        case 3: cnt = config.led_count3; break;
        case 4: cnt = config.led_count4; break;
    }
    m3ApiReturn(cnt);
}

// --- LED HSV ---

// void led_set_pixel_hsv(i32 channel, i32 pos, i32 h, i32 s, i32 v)
m3ApiRawFunction(m3_led_set_pixel_hsv)
{
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, pos);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);

    CRGB *buf = NULL;
    int count = 0;
    switch (channel) {
        case 1: buf = leds1; count = config.led_count1; break;
        case 2: buf = leds2; count = config.led_count2; break;
        case 3: buf = leds3; count = config.led_count3; break;
        case 4: buf = leds4; count = config.led_count4; break;
    }
    if (buf && pos >= 0 && pos < count) {
        CHSV hsv((uint8_t)h, (uint8_t)s, (uint8_t)v);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        rgb.r = wasm_gamma(rgb.r);
        rgb.g = wasm_gamma(rgb.g);
        rgb.b = wasm_gamma(rgb.b);
        buf[pos] = rgb;
    }

    m3ApiSuccess();
}

// void led_fill_hsv(i32 channel, i32 h, i32 s, i32 v)
m3ApiRawFunction(m3_led_fill_hsv)
{
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);

    if (channel >= 1 && channel <= 4) {
        CHSV hsv((uint8_t)h, (uint8_t)s, (uint8_t)v);
        CRGB rgb;
        hsv2rgb_rainbow(hsv, rgb);
        rgb.r = wasm_gamma(rgb.r);
        rgb.g = wasm_gamma(rgb.g);
        rgb.b = wasm_gamma(rgb.b);
        int cnt = 0;
        switch (channel) {
            case 1: cnt = config.led_count1; break;
            case 2: cnt = config.led_count2; break;
            case 3: cnt = config.led_count3; break;
            case 4: cnt = config.led_count4; break;
        }
        led_set_channel(channel, cnt, rgb);
    }

    m3ApiSuccess();
}

// i32 hsv_to_rgb(i32 h, i32 s, i32 v) -> packed (r<<16)|(g<<8)|b
m3ApiRawFunction(m3_hsv_to_rgb)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, h);
    m3ApiGetArg(int32_t, s);
    m3ApiGetArg(int32_t, v);

    CHSV hsv((uint8_t)h, (uint8_t)s, (uint8_t)v);
    CRGB rgb;
    hsv2rgb_rainbow(hsv, rgb);
    m3ApiReturn(((int32_t)rgb.r << 16) | ((int32_t)rgb.g << 8) | (int32_t)rgb.b);
}

// i32 rgb_to_hsv(i32 r, i32 g, i32 b) -> packed (h<<16)|(s<<8)|v
m3ApiRawFunction(m3_rgb_to_hsv)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);

    CRGB rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
    CHSV hsv = rgb2hsv_approximate(rgb);
    m3ApiReturn(((int32_t)hsv.h << 16) | ((int32_t)hsv.s << 8) | (int32_t)hsv.v);
}

// --- Gamma ---

// i32 led_gamma8(i32 val) -> gamma-corrected value
m3ApiRawFunction(m3_led_gamma8) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    m3ApiReturn((int32_t)gamma8[val]);
}

// void led_set_gamma(i32 enable)
m3ApiRawFunction(m3_led_set_gamma) {
    m3ApiGetArg(int32_t, enable);
    wasm_use_gamma = (enable != 0);
    m3ApiSuccess();
}

// --- Bulk LED ---

// void led_set_buffer(i32 channel, i32 rgb_ptr, i32 count)
// rgb_ptr points to count*3 bytes of interleaved R,G,B data in WASM memory
m3ApiRawFunction(m3_led_set_buffer) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, rgb_ptr);
    m3ApiGetArg(int32_t, count);

    CRGB *buf = NULL;
    int max_count = 0;
    switch (channel) {
        case 1: buf = leds1; max_count = config.led_count1; break;
        case 2: buf = leds2; max_count = config.led_count2; break;
        case 3: buf = leds3; max_count = config.led_count3; break;
        case 4: buf = leds4; max_count = config.led_count4; break;
    }
    if (!buf || count <= 0) { m3ApiSuccess(); }

    if (count > max_count) count = max_count;

    if (!wasm_mem_check(runtime, (uint32_t)rgb_ptr, (uint32_t)count * 3)) {
        m3ApiTrap("led_set_buffer: out of bounds");
    }

    for (int i = 0; i < count; i++) {
        uint8_t rgb[3];
        wasm_mem_read(runtime, (uint32_t)rgb_ptr + i * 3, rgb, 3);
        buf[i] = CRGB(wasm_gamma(rgb[0]), wasm_gamma(rgb[1]), wasm_gamma(rgb[2]));
    }

    m3ApiSuccess();
}

// --- LED array helpers ---

// void led_shift(i32 channel, i32 amount, i32 r, i32 g, i32 b)
// Positive amount shifts right, negative shifts left. New pixels get (r,g,b).
m3ApiRawFunction(m3_led_shift) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, amount);
    m3ApiGetArg(int32_t, r);
    m3ApiGetArg(int32_t, g);
    m3ApiGetArg(int32_t, b);
    int cnt;
    CRGB *buf = led_buf_for_channel(channel, &cnt);
    if (!buf || cnt == 0) { m3ApiSuccess(); }
    CRGB fill_col(r, g, b);
    if (amount > 0) {
        int shift = amount > cnt ? cnt : amount;
        memmove(buf + shift, buf, (cnt - shift) * sizeof(CRGB));
        for (int i = 0; i < shift; i++) buf[i] = fill_col;
    } else if (amount < 0) {
        int shift = (-amount) > cnt ? cnt : (-amount);
        memmove(buf, buf + shift, (cnt - shift) * sizeof(CRGB));
        for (int i = cnt - shift; i < cnt; i++) buf[i] = fill_col;
    }
    m3ApiSuccess();
}

// void led_rotate(i32 channel, i32 amount)
// Positive rotates right, negative rotates left.
m3ApiRawFunction(m3_led_rotate) {
    m3ApiGetArg(int32_t, channel);
    m3ApiGetArg(int32_t, amount);
    int cnt;
    CRGB *buf = led_buf_for_channel(channel, &cnt);
    if (!buf || cnt == 0) { m3ApiSuccess(); }
    int shift = amount % cnt;
    if (shift < 0) shift += cnt;
    if (shift == 0) { m3ApiSuccess(); }
    CRGB *tmp = (CRGB *)malloc(cnt * sizeof(CRGB));
    if (!tmp) { m3ApiSuccess(); }
    memcpy(tmp, buf, cnt * sizeof(CRGB));
    for (int i = 0; i < cnt; i++) {
        buf[(i + shift) % cnt] = tmp[i];
    }
    free(tmp);
    m3ApiSuccess();
}

// void led_reverse(i32 channel)
m3ApiRawFunction(m3_led_reverse) {
    m3ApiGetArg(int32_t, channel);
    int cnt;
    CRGB *buf = led_buf_for_channel(channel, &cnt);
    if (!buf || cnt < 2) { m3ApiSuccess(); }
    for (int i = 0; i < cnt / 2; i++) {
        CRGB tmp = buf[i];
        buf[i] = buf[cnt - 1 - i];
        buf[cnt - 1 - i] = tmp;
    }
    m3ApiSuccess();
}


// ---------- Link LED imports ----------

M3Result link_led_imports(IM3Module module)
{
    M3Result result;

    // LED core
    result = m3_LinkRawFunction(module, "env", "led_set_pixel", "v(iiiii)", m3_led_set_pixel);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "led_fill", "v(iiii)", m3_led_fill);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "led_show", "v()", m3_led_show);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "led_count", "i(i)", m3_led_count);
    if (result && result != m3Err_functionLookupFailed) return result;

    // LED HSV
    result = m3_LinkRawFunction(module, "env", "led_set_pixel_hsv", "v(iiiii)", m3_led_set_pixel_hsv);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "led_fill_hsv", "v(iiii)", m3_led_fill_hsv);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "hsv_to_rgb", "i(iii)", m3_hsv_to_rgb);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "rgb_to_hsv", "i(iii)", m3_rgb_to_hsv);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Gamma
    result = m3_LinkRawFunction(module, "env", "led_gamma8", "i(i)", m3_led_gamma8);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "led_set_gamma", "v(i)", m3_led_set_gamma);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Bulk LED
    result = m3_LinkRawFunction(module, "env", "led_set_buffer", "v(iii)", m3_led_set_buffer);
    if (result && result != m3Err_functionLookupFailed) return result;

    // LED array helpers
    result = m3_LinkRawFunction(module, "env", "led_shift", "v(iiiii)", m3_led_shift);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "led_rotate", "v(ii)", m3_led_rotate);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "led_reverse", "v(i)", m3_led_reverse);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
