#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "printManager.h"
#include "lut.h"
#include <string.h>

// --- Output ---

// void print_i32(i32 val)
m3ApiRawFunction(m3_print_i32)
{
    m3ApiGetArg(int32_t, val);
    printfnl(SOURCE_WASM, "%d\n", val);
    m3ApiSuccess();
}

// void print_f32(f32 val)
m3ApiRawFunction(m3_print_f32)
{
    m3ApiGetArg(float, val);
    printfnl(SOURCE_WASM, "%f\n", val);
    m3ApiSuccess();
}

// void print_i64(I64 val)
m3ApiRawFunction(m3_print_i64)
{
    m3ApiGetArg(int64_t, val);
    printfnl(SOURCE_WASM, "%lld\n", (long long)val);
    m3ApiSuccess();
}

// void print_f64(F64 val)
m3ApiRawFunction(m3_print_f64)
{
    m3ApiGetArg(double, val);
    printfnl(SOURCE_WASM, "%g\n", val);
    m3ApiSuccess();
}

// void print_str(i32 ptr, i32 len) — reads string from WASM linear memory
m3ApiRawFunction(m3_print_str)
{
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(int32_t, len);

    if (len < 0 || !wasm_mem_check(runtime, (uint32_t)offset, (size_t)len)) {
        m3ApiTrap("print_str: out of bounds");
    }

    // Print up to 255 chars at a time to avoid huge stack buffers
    uint32_t pos = (uint32_t)offset;
    int remaining = len;
    while (remaining > 0) {
        int chunk = remaining > 255 ? 255 : remaining;
        char buf[256];
        wasm_mem_read(runtime, pos, buf, chunk);
        buf[chunk] = '\0';
        printfnl(SOURCE_WASM, "%s", buf);
        pos += chunk;
        remaining -= chunk;
    }

    m3ApiSuccess();
}

// --- LUT ---

// i32 lut_load(i32 index) -> entry count or 0 on failure
m3ApiRawFunction(m3_lut_load)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(0);
    m3ApiReturn(loadLut((uint8_t)index));
}

// i32 lut_get(i32 index) -> value or 0 if out of bounds
m3ApiRawFunction(m3_lut_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    int v = 0;
    lut_read(index, &v);   // leaves v=0 if no LUT / out of range
    m3ApiReturn(v);
}

// i32 lut_size() -> current LUT size
m3ApiRawFunction(m3_lut_size)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(lut_get_size());
}

// void lut_set(i32 index, i32 value) — bounds-checked
m3ApiRawFunction(m3_lut_set)
{
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(int32_t, value);
    lut_write(index, value);
    m3ApiSuccess();
}

// i32 lut_save(i32 index) -> 1 on success, 0 on failure
m3ApiRawFunction(m3_lut_save)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(0);
    m3ApiReturn(saveLut((uint8_t)index));
}

// i32 lut_check(i32 index) -> entry count or -1 if not found
m3ApiRawFunction(m3_lut_check)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(-1);
    m3ApiReturn(checkLut((uint8_t)index));
}


// ---------- Link I/O imports ----------

M3Result link_io_imports(IM3Module module)
{
    M3Result result;

    // Output
    result = m3_LinkRawFunction(module, "env", "print_i32", "v(i)", m3_print_i32);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_f32", "v(f)", m3_print_f32);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_i64", "v(I)", m3_print_i64);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_f64", "v(F)", m3_print_f64);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_str", "v(ii)", m3_print_str);
    if (result && result != m3Err_functionLookupFailed) return result;

    // LUT
    result = m3_LinkRawFunction(module, "env", "lut_load", "i(i)", m3_lut_load);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_get", "i(i)", m3_lut_get);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_size", "i()", m3_lut_size);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_set", "v(ii)", m3_lut_set);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_save", "i(i)", m3_lut_save);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_check", "i(i)", m3_lut_check);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
