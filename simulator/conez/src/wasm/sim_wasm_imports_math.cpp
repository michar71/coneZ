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

    return m3Err_none;
}

#undef LINK
#undef MATH_F1
#undef MATH_F2
#undef MATH_D1
#undef MATH_D2
