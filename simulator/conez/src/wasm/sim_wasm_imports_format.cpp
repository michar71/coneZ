#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "m3_env.h"

#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <string>

// ---- wasm_vformat: portable printf engine that reads format + args from WASM memory ----
// This mirrors the firmware's wasm_format.cpp — reads a format string and a va_list
// pointer from WASM linear memory, formats using host snprintf.
//
// clang wasm32 va_list layout: arguments are 4-byte aligned, doubles are 8-byte aligned.

static int wasm_vformat(char *out, int out_size,
                        const uint8_t *mem, uint32_t mem_size,
                        uint32_t fmt_ptr, uint32_t args_ptr)
{
    if (fmt_ptr >= mem_size) return 0;

    const char *fmt = (const char *)mem + fmt_ptr;
    int pos = 0;
    uint32_t ap = args_ptr;

    #define EMIT(c) do { if (pos < out_size - 1) out[pos] = (c); pos++; } while(0)

    auto read_i32 = [&]() -> int32_t {
        if (ap + 4 > mem_size) return 0;
        int32_t v;
        memcpy(&v, mem + ap, 4);
        ap += 4;
        return v;
    };

    auto read_i64 = [&]() -> int64_t {
        ap = (ap + 7) & ~7u; // 8-byte align
        if (ap + 8 > mem_size) return 0;
        int64_t v;
        memcpy(&v, mem + ap, 8);
        ap += 8;
        return v;
    };

    auto read_f64 = [&]() -> double {
        ap = (ap + 7) & ~7u;
        if (ap + 8 > mem_size) return 0.0;
        double v;
        memcpy(&v, mem + ap, 8);
        ap += 8;
        return v;
    };

    while (*fmt) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;

        // Collect the format specifier for delegation to snprintf
        char spec_buf[64];
        int si = 0;
        spec_buf[si++] = '%';

        // Flags
        while (*fmt == '-' || *fmt == '+' || *fmt == ' ' || *fmt == '0' || *fmt == '#') {
            if (si < 60) spec_buf[si++] = *fmt;
            fmt++;
        }

        // Width
        if (*fmt == '*') {
            int w = read_i32();
            si += snprintf(spec_buf + si, 60 - si, "%d", w);
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                if (si < 60) spec_buf[si++] = *fmt;
                fmt++;
            }
        }

        // Precision
        if (*fmt == '.') {
            if (si < 60) spec_buf[si++] = '.';
            fmt++;
            if (*fmt == '*') {
                int p = read_i32();
                si += snprintf(spec_buf + si, 60 - si, "%d", p);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    if (si < 60) spec_buf[si++] = *fmt;
                    fmt++;
                }
            }
        }

        // Length modifiers (consume but track)
        int is_long = 0;
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') is_long++;
            fmt++;
        }

        char conv = *fmt;
        if (conv) fmt++;

        char tmp[128];
        int n = 0;

        switch (conv) {
        case '%': EMIT('%'); continue;

        case 'd': case 'i': {
            if (is_long >= 2) {
                int64_t v = read_i64();
                spec_buf[si++] = 'l'; spec_buf[si++] = 'l'; spec_buf[si++] = 'd'; spec_buf[si] = 0;
                n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            } else {
                int32_t v = read_i32();
                spec_buf[si++] = 'd'; spec_buf[si] = 0;
                n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            }
            break;
        }
        case 'u': {
            if (is_long >= 2) {
                uint64_t v = (uint64_t)read_i64();
                spec_buf[si++] = 'l'; spec_buf[si++] = 'l'; spec_buf[si++] = 'u'; spec_buf[si] = 0;
                n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            } else {
                uint32_t v = (uint32_t)read_i32();
                spec_buf[si++] = 'u'; spec_buf[si] = 0;
                n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            }
            break;
        }
        case 'x': case 'X': {
            uint32_t v = (uint32_t)read_i32();
            spec_buf[si++] = conv; spec_buf[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            break;
        }
        case 'c': {
            char ch = (char)read_i32();
            spec_buf[si++] = 'c'; spec_buf[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec_buf, (int)ch);
            break;
        }
        case 's': {
            uint32_t sp = (uint32_t)read_i32();
            const char *s = (sp < mem_size) ? (const char *)mem + sp : "(null)";
            spec_buf[si++] = 's'; spec_buf[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec_buf, s);
            break;
        }
        case 'f': case 'e': case 'g': case 'E': case 'G': {
            double v = read_f64();
            spec_buf[si++] = conv; spec_buf[si] = 0;
            n = snprintf(tmp, sizeof(tmp), spec_buf, v);
            break;
        }
        case 'p': {
            uint32_t v = (uint32_t)read_i32();
            n = snprintf(tmp, sizeof(tmp), "0x%x", v);
            break;
        }
        default:
            EMIT('%'); EMIT(conv); continue;
        }

        for (int i = 0; i < n; i++) EMIT(tmp[i]);
    }

    if (out_size > 0) out[pos < out_size ? pos : out_size - 1] = '\0';
    #undef EMIT
    return pos;
}

// ---- wasm_vsscanf: reads format + args from WASM memory, stores results ----

static int wasm_vsscanf(const uint8_t *mem, uint32_t mem_size,
                        uint32_t str_ptr, uint32_t fmt_ptr, uint32_t args_ptr)
{
    if (str_ptr >= mem_size || fmt_ptr >= mem_size) return 0;

    const char *str = (const char *)mem + str_ptr;
    const char *fmt = (const char *)mem + fmt_ptr;
    uint32_t ap = args_ptr;
    int matched = 0;
    int si = 0; // index into str

    while (*fmt && str[si]) {
        if (*fmt == ' ') {
            while (str[si] == ' ' || str[si] == '\t') si++;
            fmt++;
            continue;
        }
        if (*fmt != '%') {
            if (str[si] != *fmt) break;
            si++; fmt++;
            continue;
        }
        fmt++; // skip %

        // Check for assignment suppression
        bool suppress = false;
        if (*fmt == '*') { suppress = true; fmt++; }

        // Width
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        // Length
        int len_mod = 4; // default
        if (*fmt == 'h') { fmt++; len_mod = 2; if (*fmt == 'h') { fmt++; len_mod = 1; } }
        else if (*fmt == 'l') { fmt++; len_mod = 4; if (*fmt == 'l') { fmt++; len_mod = 8; } }

        char conv = *fmt;
        if (conv) fmt++;

        // Skip whitespace for numeric conversions
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'f')
            while (str[si] == ' ' || str[si] == '\t') si++;

        switch (conv) {
        case 'd': case 'i': {
            char *end;
            long v = strtol(str + si, &end, conv == 'i' ? 0 : 10);
            if (end == str + si) goto done;
            si = end - str;
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                if (dst + len_mod <= mem_size) {
                    if (len_mod == 1) *(int8_t *)((uint8_t *)mem + dst) = (int8_t)v;
                    else if (len_mod == 2) *(int16_t *)((uint8_t *)mem + dst) = (int16_t)v;
                    else *(int32_t *)((uint8_t *)mem + dst) = (int32_t)v;
                }
                matched++;
            }
            break;
        }
        case 'u': case 'x': case 'X': {
            char *end;
            unsigned long v = strtoul(str + si, &end, conv == 'u' ? 10 : 16);
            if (end == str + si) goto done;
            si = end - str;
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                if (dst + 4 <= mem_size)
                    *(uint32_t *)((uint8_t *)mem + dst) = (uint32_t)v;
                matched++;
            }
            break;
        }
        case 'f': case 'e': case 'g': {
            char *end;
            float v = strtof(str + si, &end);
            if (end == str + si) goto done;
            si = end - str;
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                if (dst + 4 <= mem_size)
                    *(float *)((uint8_t *)mem + dst) = v;
                matched++;
            }
            break;
        }
        case 's': {
            while (str[si] == ' ' || str[si] == '\t') si++;
            int start = si;
            int max_w = width > 0 ? width : 1024;
            while (str[si] && str[si] != ' ' && str[si] != '\t' && str[si] != '\n' && (si - start) < max_w) si++;
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                int slen = si - start;
                if (dst + slen + 1 <= mem_size) {
                    memcpy((uint8_t *)mem + dst, str + start, slen);
                    *((uint8_t *)mem + dst + slen) = 0;
                }
                matched++;
            }
            break;
        }
        case 'c': {
            if (!str[si]) goto done;
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                if (dst < mem_size)
                    *((uint8_t *)mem + dst) = (uint8_t)str[si];
                matched++;
            }
            si++;
            break;
        }
        case 'n': {
            if (!suppress && ap + 4 <= mem_size) {
                uint32_t dst = *(uint32_t *)((uint8_t *)mem + ap);
                ap += 4;
                if (dst + 4 <= mem_size)
                    *(int32_t *)((uint8_t *)mem + dst) = si;
                // %n doesn't count as matched
            }
            break;
        }
        case '%': {
            if (str[si] != '%') goto done;
            si++;
            break;
        }
        default: goto done;
        }
    }

done:
    return matched;
}

// ---- Import functions ----

// i32 host_printf(i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_printf) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) { m3ApiReturn(0); }

    char buf[512];
    int n = wasm_vformat(buf, sizeof(buf), mem, mem_size, fmt_ptr, args_ptr);

    auto *rt = currentRuntime();
    if (rt && n > 0) rt->emitOutput(std::string(buf, n > 511 ? 511 : n));

    m3ApiReturn(n);
}

// i32 host_snprintf(i32 buf_ptr, i32 size, i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_snprintf) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, size);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) { m3ApiReturn(0); }

    char tmp[512];
    int n = wasm_vformat(tmp, sizeof(tmp), mem, mem_size, fmt_ptr, args_ptr);

    // Copy to WASM memory
    if (buf_ptr >= 0 && size > 0 && (uint32_t)buf_ptr + size <= mem_size) {
        int copy = n < size - 1 ? n : size - 1;
        memcpy(mem + buf_ptr, tmp, copy);
        mem[buf_ptr + copy] = 0;
    }

    m3ApiReturn(n);
}

// i32 host_sscanf(i32 str_ptr, i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_sscanf) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, str_ptr);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) { m3ApiReturn(0); }

    m3ApiReturn(wasm_vsscanf(mem, mem_size, str_ptr, fmt_ptr, args_ptr));
}

// ---- Link ----

M3Result link_format_imports(IM3Module module)
{
    M3Result r;

    r = m3_LinkRawFunction(module, "env", "host_printf", "i(ii)", m3_host_printf);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "host_snprintf", "i(iiii)", m3_host_snprintf);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "host_sscanf", "i(iii)", m3_host_sscanf);
    if (r && r != m3Err_functionLookupFailed) return r;

    return m3Err_none;
}
