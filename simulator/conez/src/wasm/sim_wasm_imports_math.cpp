#include "sim_wasm_imports.h"
#include "m3_env.h"
#include <cmath>

// Float math
#define MATH_F1(name, fn) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(float); \
        m3ApiGetArg(float, x); \
        m3ApiReturn((float)fn((double)x)); \
    }
#define MATH_F2(name, fn) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(float); \
        m3ApiGetArg(float, x); \
        m3ApiGetArg(float, y); \
        m3ApiReturn((float)fn((double)x, (double)y)); \
    }

MATH_F1(sinf, sin)
MATH_F1(cosf, cos)
MATH_F1(tanf, tan)
MATH_F1(asinf, asin)
MATH_F1(acosf, acos)
MATH_F1(atanf, atan)
MATH_F2(atan2f, atan2)
MATH_F2(powf, pow)
MATH_F1(expf, exp)
MATH_F1(logf, log)
MATH_F1(log2f, log2)
MATH_F2(fmodf, fmod)

// Double math
#define MATH_D1(name, fn) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(double); \
        m3ApiGetArg(double, x); \
        m3ApiReturn(fn(x)); \
    }
#define MATH_D2(name, fn) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(double); \
        m3ApiGetArg(double, x); \
        m3ApiGetArg(double, y); \
        m3ApiReturn(fn(x, y)); \
    }

MATH_D1(sin, sin)
MATH_D1(cos, cos)
MATH_D1(tan, tan)
MATH_D1(asin, asin)
MATH_D1(acos, acos)
MATH_D1(atan, atan)
MATH_D2(atan2, atan2)
MATH_D2(pow, pow)
MATH_D1(exp, exp)
MATH_D1(log, log)
MATH_D1(log2, log2)
MATH_D2(fmod, fmod)

// ---- Curve functions ----

static float sim_lerp(float a, float b, float t) {
    return a + t * (b - a);
}

static float sim_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

m3ApiRawFunction(m3_lerp) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, a);
    m3ApiGetArg(float, b);
    m3ApiGetArg(float, t);
    m3ApiReturn(sim_lerp(a, b, t));
}

m3ApiRawFunction(m3_larp) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x_pos);
    m3ApiGetArg(int32_t, x_min);
    m3ApiGetArg(int32_t, x_max);
    m3ApiGetArg(int32_t, min_val);
    m3ApiGetArg(int32_t, max_val);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(int32_t, window);
    m3ApiGetArg(int32_t, stride);

    if (x_min == x_max) { m3ApiReturn(min_val); }
    int range = x_max - x_min;
    int offset_int = (range / 2) * offset / 100;
    int window_int = offset_int * window / 100;
    if (stride < 1) stride = 1;
    float sum = 0; int count = 0;
    for (int i = x_pos - (window_int / 2); i <= x_pos + (window_int / 2); i += stride) {
        if (i < x_min) { sum += min_val; }
        else if (i > x_max) { sum += max_val; }
        else {
            int active_min = x_min + offset_int;
            int active_max = x_max - offset_int;
            float t = (float)(i - active_min) / (active_max - active_min);
            t = sim_clampf(t, 0.0f, 1.0f);
            sum += sim_lerp((float)min_val, (float)max_val, t);
        }
        count++;
    }
    m3ApiReturn(count > 0 ? (int32_t)roundf(sum / count) : min_val);
}

m3ApiRawFunction(m3_larpf) {
    m3ApiReturnType(float);
    m3ApiGetArg(float, x_pos);
    m3ApiGetArg(float, x_min);
    m3ApiGetArg(float, x_max);
    m3ApiGetArg(float, min_val);
    m3ApiGetArg(float, max_val);
    m3ApiGetArg(float, offset);
    m3ApiGetArg(float, window);
    m3ApiGetArg(int32_t, stride);

    if (x_min == x_max) { m3ApiReturn(min_val); }
    float range = x_max - x_min;
    float offset_f = (range / 2.0f) * offset / 100.0f;
    float window_f = offset_f * window / 100.0f;
    if (stride < 1) stride = 1;
    float step = window_f / stride;
    if (step < 0.001f) step = 1.0f;
    float sum = 0; int count = 0;
    for (float s = x_pos - (window_f / 2.0f); s <= x_pos + (window_f / 2.0f); s += step) {
        if (s < x_min) { sum += min_val; }
        else if (s > x_max) { sum += max_val; }
        else {
            float active_min = x_min + offset_f;
            float active_max = x_max - offset_f;
            float t = (s - active_min) / (active_max - active_min);
            t = sim_clampf(t, 0.0f, 1.0f);
            sum += sim_lerp(min_val, max_val, t);
        }
        count++;
    }
    m3ApiReturn(count > 0 ? sum / count : min_val);
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_math_imports(IM3Module module)
{
    M3Result r;

    // Float
    LINK("sinf",   "f(f)",  m3_sinf)
    LINK("cosf",   "f(f)",  m3_cosf)
    LINK("tanf",   "f(f)",  m3_tanf)
    LINK("asinf",  "f(f)",  m3_asinf)
    LINK("acosf",  "f(f)",  m3_acosf)
    LINK("atanf",  "f(f)",  m3_atanf)
    LINK("atan2f", "f(ff)", m3_atan2f)
    LINK("powf",   "f(ff)", m3_powf)
    LINK("expf",   "f(f)",  m3_expf)
    LINK("logf",   "f(f)",  m3_logf)
    LINK("log2f",  "f(f)",  m3_log2f)
    LINK("fmodf",  "f(ff)", m3_fmodf)

    // Double
    LINK("sin",   "F(F)",  m3_sin)
    LINK("cos",   "F(F)",  m3_cos)
    LINK("tan",   "F(F)",  m3_tan)
    LINK("asin",  "F(F)",  m3_asin)
    LINK("acos",  "F(F)",  m3_acos)
    LINK("atan",  "F(F)",  m3_atan)
    LINK("atan2", "F(FF)", m3_atan2)
    LINK("pow",   "F(FF)", m3_pow)
    LINK("exp",   "F(F)",  m3_exp)
    LINK("log",   "F(F)",  m3_log)
    LINK("log2",  "F(F)",  m3_log2)
    LINK("fmod",  "F(FF)", m3_fmod)

    // Curve
    LINK("lerp",  "f(fff)",      m3_lerp)
    LINK("larp",  "i(iiiiiiii)", m3_larp)
    LINK("larpf", "f(fffffffi)", m3_larpf)

    return m3Err_none;
}

#undef LINK
#undef MATH_F1
#undef MATH_F2
#undef MATH_D1
#undef MATH_D2
