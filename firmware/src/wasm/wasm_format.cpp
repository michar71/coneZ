#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "printManager.h"
#include <string.h>
#include <stdio.h>

// --- Host-side printf/snprintf ---
// Parses a format string from WASM memory, reads va_list args from WASM memory,
// formats using the platform's snprintf.  Assumes wasm32 clang va_list layout:
// all args at 4-byte-aligned offsets, doubles are 8 bytes at 4-byte alignment.

static int wasm_vformat(IM3Runtime runtime,
                        uint32_t fmt_off, uint32_t args_off,
                        char *out, int out_size)
{
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || fmt_off >= mem_size) return -1;

    const char *fmt = (const char *)(mem + fmt_off);
    int fmt_len = 0;
    while (fmt_off + fmt_len < mem_size && fmt[fmt_len]) fmt_len++;

    uint32_t aoff = args_off;
    int pos = 0;
    int err = 0;

    for (int i = 0; i < fmt_len && !err; i++) {
        if (fmt[i] != '%') {
            if (pos < out_size - 1) out[pos] = fmt[i];
            pos++;
            continue;
        }
        i++;
        if (i >= fmt_len) break;
        if (fmt[i] == '%') {
            if (pos < out_size - 1) out[pos] = '%';
            pos++;
            continue;
        }

        // Build format specifier for host snprintf
        char spec[32];
        int sp = 0;
        spec[sp++] = '%';

        // Flags
        while (i < fmt_len && (fmt[i] == '-' || fmt[i] == '0' ||
               fmt[i] == ' ' || fmt[i] == '+' || fmt[i] == '#')) {
            if (sp < 28) spec[sp++] = fmt[i];
            i++;
        }

        // Width
        if (i < fmt_len && fmt[i] == '*') {
            if (aoff + 4 > mem_size) { err = 1; break; }
            int32_t w; memcpy(&w, mem + aoff, 4); aoff += 4;
            sp += snprintf(spec + sp, 28 - sp, "%d", (int)w);
            i++;
        } else {
            while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                if (sp < 28) spec[sp++] = fmt[i];
                i++;
            }
        }

        // Precision
        if (i < fmt_len && fmt[i] == '.') {
            if (sp < 28) spec[sp++] = '.';
            i++;
            if (i < fmt_len && fmt[i] == '*') {
                if (aoff + 4 > mem_size) { err = 1; break; }
                int32_t p; memcpy(&p, mem + aoff, 4); aoff += 4;
                sp += snprintf(spec + sp, 28 - sp, "%d", (int)p);
                i++;
            } else {
                while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                    if (sp < 28) spec[sp++] = fmt[i];
                    i++;
                }
            }
        }

        // Length modifiers — skip
        while (i < fmt_len && (fmt[i] == 'l' || fmt[i] == 'h' ||
               fmt[i] == 'z' || fmt[i] == 'j' || fmt[i] == 't'))
            i++;

        if (i >= fmt_len) break;
        char conv = fmt[i];

        char tmp[192];
        int n = 0;

        switch (conv) {
        case 'd': case 'i':
            if (sp < 30) spec[sp++] = conv;
            spec[sp] = '\0';
            if (aoff + 4 > mem_size) { err = 1; break; }
            { int32_t v; memcpy(&v, mem + aoff, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (int)v); }
            break;

        case 'u': case 'x': case 'X':
            if (sp < 30) spec[sp++] = conv;
            spec[sp] = '\0';
            if (aoff + 4 > mem_size) { err = 1; break; }
            { uint32_t v; memcpy(&v, mem + aoff, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (unsigned)v); }
            break;

        case 'c':
            if (sp < 30) spec[sp++] = 'c';
            spec[sp] = '\0';
            if (aoff + 4 > mem_size) { err = 1; break; }
            { int32_t v; memcpy(&v, mem + aoff, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (char)v); }
            break;

        case 'f':
            if (sp < 30) spec[sp++] = 'f';
            spec[sp] = '\0';
            if (aoff + 8 > mem_size) { err = 1; break; }
            { double v; memcpy(&v, mem + aoff, 8); aoff += 8;
              n = snprintf(tmp, sizeof(tmp), spec, v); }
            break;

        case 'e': case 'g':
            if (sp < 30) spec[sp++] = conv;
            spec[sp] = '\0';
            if (aoff + 8 > mem_size) { err = 1; break; }
            { double v; memcpy(&v, mem + aoff, 8); aoff += 8;
              n = snprintf(tmp, sizeof(tmp), spec, v); }
            break;

        case 's':
            if (sp < 30) spec[sp++] = 's';
            spec[sp] = '\0';
            if (aoff + 4 > mem_size) { err = 1; break; }
            { uint32_t sptr; memcpy(&sptr, mem + aoff, 4); aoff += 4;
              // Copy string from WASM memory with bounds check
              char sbuf[128];
              if (sptr > 0 && sptr < mem_size) {
                  const char *s = (const char *)(mem + sptr);
                  uint32_t max_len = mem_size - sptr;
                  if (max_len > 127) max_len = 127;
                  uint32_t slen = 0;
                  while (slen < max_len && s[slen]) slen++;
                  memcpy(sbuf, s, slen);
                  sbuf[slen] = '\0';
              } else {
                  strcpy(sbuf, "(null)");
              }
              n = snprintf(tmp, sizeof(tmp), spec, sbuf);
            }
            break;

        case 'p':
            spec[sp] = '\0';
            if (aoff + 4 > mem_size) { err = 1; break; }
            { uint32_t v; memcpy(&v, mem + aoff, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), "0x%x", (unsigned)v); }
            break;

        default:
            n = snprintf(tmp, sizeof(tmp), "%%%c", conv);
            break;
        }

        if (err) break;

        for (int j = 0; j < n; j++) {
            if (pos < out_size - 1) out[pos] = tmp[j];
            pos++;
        }
    }

    if (out_size > 0) {
        int term = pos < out_size ? pos : out_size - 1;
        out[term] = '\0';
    }
    return err ? -1 : pos;
}

// int host_printf(i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_printf)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    char buf[256];
    int r = wasm_vformat(runtime, (uint32_t)fmt_ptr, (uint32_t)args_ptr,
                         buf, sizeof(buf));
    if (r > 0) printfnl(SOURCE_WASM, "%s", buf);
    m3ApiReturn(r >= 0 ? r : 0);
}

// int host_snprintf(i32 buf_ptr, i32 buf_size, i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_snprintf)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, buf_size);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    char tmp[512];
    int r = wasm_vformat(runtime, (uint32_t)fmt_ptr, (uint32_t)args_ptr,
                         tmp, sizeof(tmp));
    if (r < 0) m3ApiReturn(-1);

    // Copy formatted output to WASM memory
    if (buf_size > 0) {
        uint32_t mem_size = 0;
        uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
        if (mem && (uint32_t)buf_ptr + (uint32_t)buf_size <= mem_size) {
            int avail = (int)sizeof(tmp) - 1;
            int copy = r < avail ? r : avail;
            if (copy > buf_size - 1) copy = buf_size - 1;
            memcpy(mem + buf_ptr, tmp, copy);
            mem[buf_ptr + copy] = '\0';
        }
    }

    m3ApiReturn(r);
}

// --- sscanf (reverse of printf — parse formatted string into WASM memory) ---

// Walks format string one conversion at a time, using host sscanf for each field.
// Output pointers are read from the WASM va_list (4-byte WASM memory offsets).
// Length modifiers: hh→1B, h→2B, (none/l)→4B, ll→8B.  %lf→double(8B), %f→float(4B).
static int wasm_vsscanf(IM3Runtime runtime,
                        uint32_t str_off, uint32_t fmt_off, uint32_t args_off)
{
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || str_off >= mem_size || fmt_off >= mem_size) return -1;

    const char *str_base = (const char *)(mem + str_off);
    const char *fmt_base = (const char *)(mem + fmt_off);

    int str_len = 0;
    while (str_off + str_len < mem_size && str_base[str_len]) str_len++;
    int fmt_len = 0;
    while (fmt_off + fmt_len < mem_size && fmt_base[fmt_len]) fmt_len++;

    // Local copies for safe null-terminated access
    char str_buf[512], fmt_buf[256];
    int sc = str_len < (int)sizeof(str_buf) - 1 ? str_len : (int)sizeof(str_buf) - 1;
    memcpy(str_buf, str_base, sc); str_buf[sc] = '\0';
    int fc = fmt_len < (int)sizeof(fmt_buf) - 1 ? fmt_len : (int)sizeof(fmt_buf) - 1;
    memcpy(fmt_buf, fmt_base, fc); fmt_buf[fc] = '\0';

    uint32_t aoff = args_off;
    int matches = 0;
    int spos = 0; // current position in input string

    for (int i = 0; i < fc; i++) {
        if (fmt_buf[i] != '%') {
            if (fmt_buf[i] == ' ' || fmt_buf[i] == '\t' || fmt_buf[i] == '\n') {
                while (str_buf[spos] && (str_buf[spos] == ' ' ||
                       str_buf[spos] == '\t' || str_buf[spos] == '\n'))
                    spos++;
            } else {
                if (str_buf[spos] != fmt_buf[i]) return matches;
                spos++;
            }
            continue;
        }
        i++;
        if (i >= fc) break;
        if (fmt_buf[i] == '%') {
            if (str_buf[spos] != '%') return matches;
            spos++;
            continue;
        }

        // Assignment suppression
        bool suppress = false;
        if (fmt_buf[i] == '*') { suppress = true; i++; }

        // Width
        char wstr[16] = "";
        int wp = 0;
        while (i < fc && fmt_buf[i] >= '0' && fmt_buf[i] <= '9') {
            if (wp < 14) wstr[wp++] = fmt_buf[i];
            i++;
        }
        wstr[wp] = '\0';

        // Length modifier: 0=none, 1=h, 2=hh, 3=l, 4=ll
        int lmod = 0;
        if (i < fc && fmt_buf[i] == 'h') {
            i++; lmod = 1;
            if (i < fc && fmt_buf[i] == 'h') { i++; lmod = 2; }
        } else if (i < fc && fmt_buf[i] == 'l') {
            i++; lmod = 3;
            if (i < fc && fmt_buf[i] == 'l') { i++; lmod = 4; }
        } else if (i < fc && fmt_buf[i] == 'z') {
            i++; lmod = 0;
        }
        if (i >= fc) break;
        char conv = fmt_buf[i];

        // Skip whitespace for most conversions
        if (conv != 'c' && conv != 'n') {
            while (str_buf[spos] == ' ' || str_buf[spos] == '\t' || str_buf[spos] == '\n')
                spos++;
        }
        if (str_buf[spos] == '\0' && conv != 'n') return matches;

        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
            char sfmt[32];
            int consumed = 0;
            bool is_unsigned = (conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o');

            if (lmod == 4) { // ll → 64-bit
                long long llv = 0;
                unsigned long long ullv = 0;
                if (is_unsigned) {
                    snprintf(sfmt, sizeof(sfmt), "%%%sll%c%%n", wstr, conv);
                    if (sscanf(str_buf + spos, sfmt, &ullv, &consumed) < 1) return matches;
                } else {
                    snprintf(sfmt, sizeof(sfmt), "%%%slld%%n", wstr);
                    if (sscanf(str_buf + spos, sfmt, &llv, &consumed) < 1) return matches;
                }
                spos += consumed;
                if (!suppress) {
                    if (aoff + 4 > mem_size) return matches;
                    uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                    if (dst + 8 > mem_size) return matches;
                    int64_t v64 = is_unsigned ? (int64_t)ullv : (int64_t)llv;
                    memcpy(mem + dst, &v64, 8);
                    matches++;
                }
            } else { // 32-bit or narrower
                int32_t iv = 0;
                uint32_t uv = 0;
                snprintf(sfmt, sizeof(sfmt), "%%%s%c%%n", wstr, conv);
                if (is_unsigned) {
                    if (sscanf(str_buf + spos, sfmt, &uv, &consumed) < 1) return matches;
                } else {
                    if (sscanf(str_buf + spos, sfmt, &iv, &consumed) < 1) return matches;
                }
                spos += consumed;
                if (!suppress) {
                    if (aoff + 4 > mem_size) return matches;
                    uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                    int32_t val = is_unsigned ? (int32_t)uv : iv;
                    if (lmod == 2) { // hh → 1 byte
                        if (dst + 1 > mem_size) return matches;
                        mem[dst] = (uint8_t)val;
                    } else if (lmod == 1) { // h → 2 bytes
                        if (dst + 2 > mem_size) return matches;
                        int16_t h = (int16_t)val;
                        memcpy(mem + dst, &h, 2);
                    } else { // none/l → 4 bytes
                        if (dst + 4 > mem_size) return matches;
                        if (is_unsigned)
                            memcpy(mem + dst, &uv, 4);
                        else
                            memcpy(mem + dst, &iv, 4);
                    }
                    matches++;
                }
            }
            break;
        }

        case 'f': case 'e': case 'g': case 'E': case 'G': {
            char sfmt[32];
            int consumed = 0;
            if (lmod >= 3) { // l or ll → double (8 bytes)
                double dv = 0;
                snprintf(sfmt, sizeof(sfmt), "%%%slf%%n", wstr);
                if (sscanf(str_buf + spos, sfmt, &dv, &consumed) < 1) return matches;
                spos += consumed;
                if (!suppress) {
                    if (aoff + 4 > mem_size) return matches;
                    uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                    if (dst + 8 > mem_size) return matches;
                    memcpy(mem + dst, &dv, 8);
                    matches++;
                }
            } else { // default → float (4 bytes)
                float fv = 0;
                snprintf(sfmt, sizeof(sfmt), "%%%sf%%n", wstr);
                if (sscanf(str_buf + spos, sfmt, &fv, &consumed) < 1) return matches;
                spos += consumed;
                if (!suppress) {
                    if (aoff + 4 > mem_size) return matches;
                    uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                    if (dst + 4 > mem_size) return matches;
                    memcpy(mem + dst, &fv, 4);
                    matches++;
                }
            }
            break;
        }

        case 's': {
            char sbuf[256];
            char sfmt[32];
            int consumed = 0;
            int max_w = wp > 0 ? atoi(wstr) : 255;
            if (max_w > 255) max_w = 255;
            snprintf(sfmt, sizeof(sfmt), "%%%ds%%n", max_w);
            if (sscanf(str_buf + spos, sfmt, sbuf, &consumed) < 1) return matches;
            spos += consumed;
            if (!suppress) {
                if (aoff + 4 > mem_size) return matches;
                uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                int slen = strlen(sbuf) + 1;
                if (dst + (uint32_t)slen > mem_size) return matches;
                memcpy(mem + dst, sbuf, slen);
                matches++;
            }
            break;
        }

        case 'c': {
            if (str_buf[spos] == '\0') return matches;
            if (!suppress) {
                if (aoff + 4 > mem_size) return matches;
                uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                if (dst + 1 > mem_size) return matches;
                mem[dst] = (uint8_t)str_buf[spos];
                matches++;
            }
            spos++;
            break;
        }

        case 'n': {
            if (!suppress) {
                if (aoff + 4 > mem_size) return matches;
                uint32_t dst; memcpy(&dst, mem + aoff, 4); aoff += 4;
                if (dst + 4 > mem_size) return matches;
                int32_t pos = spos;
                memcpy(mem + dst, &pos, 4);
            }
            break;
        }

        default:
            return matches;
        }
    }

    return matches;
}

// int host_sscanf(i32 str_ptr, i32 fmt_ptr, i32 args_ptr)
m3ApiRawFunction(m3_host_sscanf)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, str_ptr);
    m3ApiGetArg(int32_t, fmt_ptr);
    m3ApiGetArg(int32_t, args_ptr);

    int r = wasm_vsscanf(runtime, (uint32_t)str_ptr, (uint32_t)fmt_ptr,
                         (uint32_t)args_ptr);
    m3ApiReturn(r);
}


// ---------- Link format imports ----------

M3Result link_format_imports(IM3Module module)
{
    M3Result result;

    result = m3_LinkRawFunction(module, "env", "host_printf", "i(ii)", m3_host_printf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "host_snprintf", "i(iiii)", m3_host_snprintf);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "host_sscanf", "i(iii)", m3_host_sscanf);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
