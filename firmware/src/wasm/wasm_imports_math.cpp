#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include <math.h>

// --- Math (transcendentals — no WASM instruction, backed by platform libm) ---

m3ApiRawFunction(m3_sinf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(sinf(x));
}

m3ApiRawFunction(m3_cosf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(cosf(x));
}

m3ApiRawFunction(m3_tanf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(tanf(x));
}

m3ApiRawFunction(m3_asinf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(asinf(x));
}

m3ApiRawFunction(m3_acosf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(acosf(x));
}

m3ApiRawFunction(m3_atanf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(atanf(x));
}

m3ApiRawFunction(m3_atan2f)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, y);
    m3ApiGetArg(float, x);
    m3ApiReturn(atan2f(y, x));
}

m3ApiRawFunction(m3_powf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, base);
    m3ApiGetArg(float, exp);
    m3ApiReturn(powf(base, exp));
}

m3ApiRawFunction(m3_expf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(expf(x));
}

m3ApiRawFunction(m3_logf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(logf(x));
}

m3ApiRawFunction(m3_log2f)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiReturn(log2f(x));
}

m3ApiRawFunction(m3_fmodf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x);
    m3ApiGetArg(float, y);
    m3ApiReturn(fmodf(x, y));
}


// --- Math (double-precision transcendentals) ---

m3ApiRawFunction(m3_sin)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(sin(x));
}

m3ApiRawFunction(m3_cos)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(cos(x));
}

m3ApiRawFunction(m3_tan)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(tan(x));
}

m3ApiRawFunction(m3_asin)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(asin(x));
}

m3ApiRawFunction(m3_acos)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(acos(x));
}

m3ApiRawFunction(m3_atan)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(atan(x));
}

m3ApiRawFunction(m3_atan2)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, y);
    m3ApiGetArg(double, x);
    m3ApiReturn(atan2(y, x));
}

m3ApiRawFunction(m3_pow)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, base);
    m3ApiGetArg(double, exp);
    m3ApiReturn(pow(base, exp));
}

m3ApiRawFunction(m3_exp)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(exp(x));
}

m3ApiRawFunction(m3_log)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(log(x));
}

m3ApiRawFunction(m3_log2)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiReturn(log2(x));
}

m3ApiRawFunction(m3_fmod)
{
    m3ApiReturnType(double);
    m3ApiGetArg(double, x);
    m3ApiGetArg(double, y);
    m3ApiReturn(fmod(x, y));
}


// --- Curve functions ---

#include "curve.h"

m3ApiRawFunction(m3_lerp)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, a);
    m3ApiGetArg(float, b);
    m3ApiGetArg(float, t);
    m3ApiReturn(lerp(a, b, t));
}

m3ApiRawFunction(m3_larp)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, x_pos);
    m3ApiGetArg(int32_t, x_min);
    m3ApiGetArg(int32_t, x_max);
    m3ApiGetArg(int32_t, min_val);
    m3ApiGetArg(int32_t, max_val);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(int32_t, window);
    m3ApiGetArg(int32_t, stride);
    m3ApiReturn(larp(x_pos, x_min, x_max, min_val, max_val, offset, window, stride));
}

m3ApiRawFunction(m3_larpf)
{
    m3ApiReturnType(float);
    m3ApiGetArg(float, x_pos);
    m3ApiGetArg(float, x_min);
    m3ApiGetArg(float, x_max);
    m3ApiGetArg(float, min_val);
    m3ApiGetArg(float, max_val);
    m3ApiGetArg(float, offset);
    m3ApiGetArg(float, window);
    m3ApiGetArg(int32_t, stride);
    m3ApiReturn(larpf(x_pos, x_min, x_max, min_val, max_val, offset, window, stride));
}


// ---------- Link math imports ----------

M3Result link_math_imports(IM3Module module)
{
    M3Result result;

    result = m3_LinkRawFunction(module, "env", "sinf", "f(f)", m3_sinf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "cosf", "f(f)", m3_cosf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "tanf", "f(f)", m3_tanf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "asinf", "f(f)", m3_asinf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "acosf", "f(f)", m3_acosf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "atanf", "f(f)", m3_atanf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "atan2f", "f(ff)", m3_atan2f);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "powf", "f(ff)", m3_powf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "expf", "f(f)", m3_expf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "logf", "f(f)", m3_logf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "log2f", "f(f)", m3_log2f);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "fmodf", "f(ff)", m3_fmodf);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Double-precision
    result = m3_LinkRawFunction(module, "env", "sin", "F(F)", m3_sin);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "cos", "F(F)", m3_cos);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "tan", "F(F)", m3_tan);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "asin", "F(F)", m3_asin);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "acos", "F(F)", m3_acos);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "atan", "F(F)", m3_atan);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "atan2", "F(FF)", m3_atan2);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "pow", "F(FF)", m3_pow);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "exp", "F(F)", m3_exp);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "log", "F(F)", m3_log);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "log2", "F(F)", m3_log2);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "fmod", "F(FF)", m3_fmod);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Curve functions
    result = m3_LinkRawFunction(module, "env", "lerp", "f(fff)", m3_lerp);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "larp", "i(iiiiiiii)", m3_larp);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "larpf", "f(fffffffi)", m3_larpf);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
