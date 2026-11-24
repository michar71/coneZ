#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "printManager.h"
#include <string.h>
#include <stdio.h>

// --- Host-side printf/snprintf ---
// Parses a format string from WASM memory, reads va_list args from WASM memory,
// formats using the platform's snprintf.  Assumes the wasm32 clang va_list
// layout that c2wasm emits (tools/c2wasm/expr.c) and the simulator reads:
// 32-bit args in 4-byte cells; double and long long are 8 bytes at an
// 8-BYTE-ALIGNED offset. So every 8-byte read rounds the cursor up first.
#define WASM_VA_ALIGN8(off)  (((off) + 7u) & ~7u)

static int wasm_vformat(IM3Runtime runtime,
                        uint32_t fmt_off, uint32_t args_off,
                        char *out, int out_size)
{
    uint32_t mem_size = wasm_mem_size(runtime);
    if (fmt_off >= mem_size) return -1;

    // Read format string into local buffer
    char fmt[512];
    int fmt_len = wasm_mem_read_str(runtime, fmt_off, fmt, sizeof(fmt));
    if (fmt_len < 0) return -1;

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

        // Also tracked numerically: %s is emitted straight from guest memory
        // (not through snprintf), so it applies these itself.
        bool left = false;
        int  width = 0;
        int  prec = -1;

        // Flags
        while (i < fmt_len && (fmt[i] == '-' || fmt[i] == '0' ||
               fmt[i] == ' ' || fmt[i] == '+' || fmt[i] == '#')) {
            if (fmt[i] == '-') left = true;
            if (sp < 28) spec[sp++] = fmt[i];
            i++;
        }

        // Width
        if (i < fmt_len && fmt[i] == '*') {
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            int32_t w; wasm_mem_read(runtime, aoff, &w, 4); aoff += 4;
            if (w < -999) w = -999;
            if (w > 999) w = 999;
            if (w < 0) { left = true; width = -w; } else width = w;
            if (sp < (int)sizeof(spec) - 1) {
                int wn = snprintf(spec + sp, sizeof(spec) - sp, "%d", (int)w);
                if (wn > 0) sp += wn;
            }
            i++;
        } else {
            while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                if (sp < 28) spec[sp++] = fmt[i];
                if (width < 999) width = width * 10 + (fmt[i] - '0');
                i++;
            }
            if (width > 999) width = 999;
        }

        // Precision
        if (i < fmt_len && fmt[i] == '.') {
            if (sp < 28) spec[sp++] = '.';
            i++;
            if (i < fmt_len && fmt[i] == '*') {
                if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
                int32_t p; wasm_mem_read(runtime, aoff, &p, 4); aoff += 4;
                if (p < 0) p = 0;
                if (p > 999) p = 999;
                prec = p;
                if (sp < (int)sizeof(spec) - 1) {
                    int pn = snprintf(spec + sp, sizeof(spec) - sp, "%d", (int)p);
                    if (pn > 0) sp += pn;
                }
                i++;
            } else {
                prec = 0;       // "%.s" == "%.0s"
                while (i < fmt_len && fmt[i] >= '0' && fmt[i] <= '9') {
                    if (sp < 28) spec[sp++] = fmt[i];
                    if (prec < 999) prec = prec * 10 + (fmt[i] - '0');
                    i++;
                }
                if (prec > 999) prec = 999;
            }
        }

        // Length modifiers. Only "ll" changes the argument size (an 8-byte,
        // 8-aligned i64); l/h/z/j/t are all 32-bit on wasm32.
        int lcount = 0;
        while (i < fmt_len && (fmt[i] == 'l' || fmt[i] == 'h' ||
               fmt[i] == 'z' || fmt[i] == 'j' || fmt[i] == 't')) {
            if (fmt[i] == 'l') lcount++;
            i++;
        }
        bool is_ll = (lcount >= 2);

        if (i >= fmt_len) break;
        char conv = fmt[i];

        // A hostile format ("%*.*", long flag runs) can drive sp past the
        // buffer; keep every following spec[sp] write (up to "ll" + conv + NUL)
        // in bounds.
        if (sp > (int)sizeof(spec) - 4) sp = (int)sizeof(spec) - 4;

        char tmp[192];
        int n = 0;

        switch (conv) {
        case 'd': case 'i':
            if (is_ll) {
                // long long: reading only 4 bytes printed the low word and left
                // the high word to be consumed as the NEXT argument.
                spec[sp++] = 'l'; spec[sp++] = 'l'; spec[sp++] = conv; spec[sp] = '\0';
                aoff = WASM_VA_ALIGN8(aoff);
                if (!wasm_mem_check(runtime, aoff, 8)) { err = 1; break; }
                int64_t v; wasm_mem_read(runtime, aoff, &v, 8); aoff += 8;
                n = snprintf(tmp, sizeof(tmp), spec, (long long)v);
                break;
            }
            spec[sp++] = conv;
            spec[sp] = '\0';
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            { int32_t v; wasm_mem_read(runtime, aoff, &v, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (int)v); }
            break;

        case 'u': case 'x': case 'X': case 'o':
            if (is_ll) {
                spec[sp++] = 'l'; spec[sp++] = 'l'; spec[sp++] = conv; spec[sp] = '\0';
                aoff = WASM_VA_ALIGN8(aoff);
                if (!wasm_mem_check(runtime, aoff, 8)) { err = 1; break; }
                uint64_t v; wasm_mem_read(runtime, aoff, &v, 8); aoff += 8;
                n = snprintf(tmp, sizeof(tmp), spec, (unsigned long long)v);
                break;
            }
            spec[sp++] = conv;
            spec[sp] = '\0';
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            { uint32_t v; wasm_mem_read(runtime, aoff, &v, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (unsigned)v); }
            break;

        case 'c':
            if (sp < 30) spec[sp++] = 'c';
            spec[sp] = '\0';
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            { int32_t v; wasm_mem_read(runtime, aoff, &v, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), spec, (char)v); }
            break;

        case 'f': case 'e': case 'g': case 'E': case 'G':
            // A double sits at an 8-byte-ALIGNED offset: after an odd number of
            // 32-bit args there is a 4-byte gap before it. Reading at the
            // unaligned cursor printed garbage and shifted every later arg.
            spec[sp++] = conv;
            spec[sp] = '\0';
            aoff = WASM_VA_ALIGN8(aoff);
            if (!wasm_mem_check(runtime, aoff, 8)) { err = 1; break; }
            { double v; wasm_mem_read(runtime, aoff, &v, 8); aoff += 8;
              n = snprintf(tmp, sizeof(tmp), spec, v); }
            break;

        case 's':
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            { uint32_t sptr; wasm_mem_read(runtime, aoff, &sptr, 4); aoff += 4;
              // Emitted straight from guest memory, with width/precision
              // applied here: going through a 128-byte copy and snprintf cut
              // every string at 127 chars. An out-of-bounds or unterminated
              // pointer prints "(null)".
              const char *lit = NULL;
              int slen = wasm_mem_strlen(runtime, sptr);
              if (slen < 0) { lit = "(null)"; slen = 6; }
              if (prec >= 0 && slen > prec) slen = prec;
              int pad = (width > slen) ? width - slen : 0;
              if (!left)
                  for (int k = 0; k < pad; k++) { if (pos < out_size - 1) out[pos] = ' '; pos++; }
              for (int k = 0; k < slen; ) {
                  char chunk[64];
                  int take = slen - k;
                  if (take > (int)sizeof(chunk)) take = (int)sizeof(chunk);
                  if (lit) memcpy(chunk, lit + k, take);
                  else     wasm_mem_read(runtime, sptr + (uint32_t)k, chunk, take);
                  for (int j = 0; j < take; j++) { if (pos < out_size - 1) out[pos] = chunk[j]; pos++; }
                  k += take;
              }
              if (left)
                  for (int k = 0; k < pad; k++) { if (pos < out_size - 1) out[pos] = ' '; pos++; }
              n = 0;
            }
            break;

        case 'p':
            spec[sp] = '\0';
            if (!wasm_mem_check(runtime, aoff, 4)) { err = 1; break; }
            { uint32_t v; wasm_mem_read(runtime, aoff, &v, 4); aoff += 4;
              n = snprintf(tmp, sizeof(tmp), "0x%x", (unsigned)v); }
            break;

        default:
            n = snprintf(tmp, sizeof(tmp), "%%%c", conv);
            break;
        }

        if (err) break;

        // snprintf returns the length it WOULD have written; tmp holds at most
        // sizeof(tmp)-1 valid bytes. Clamp before copying or a wide field
        // ("%600d") would read past tmp and leak host stack into guest output.
        if (n < 0) n = 0;
        if (n > (int)sizeof(tmp) - 1) n = (int)sizeof(tmp) - 1;

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
    if (buf_size > 0 && wasm_mem_check(runtime, (uint32_t)buf_ptr, (uint32_t)buf_size)) {
        int avail = (int)sizeof(tmp) - 1;
        int copy = r < avail ? r : avail;
        if (copy > buf_size - 1) copy = buf_size - 1;
        wasm_mem_write(runtime, (uint32_t)buf_ptr, tmp, copy);
        uint8_t nul = 0;
        wasm_mem_write8(runtime, (uint32_t)buf_ptr + copy, nul);
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
    uint32_t mem_size = wasm_mem_size(runtime);
    if (str_off >= mem_size || fmt_off >= mem_size) return -1;

    // Local copies for safe null-terminated access
    char str_buf[512], fmt_buf[256];
    int sc = wasm_mem_read_str(runtime, str_off, str_buf, sizeof(str_buf));
    int fc = wasm_mem_read_str(runtime, fmt_off, fmt_buf, sizeof(fmt_buf));
    if (sc < 0 || fc < 0) return -1;

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
                    if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                    uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                    if (!wasm_mem_check(runtime, dst, 8)) return matches;
                    int64_t v64 = is_unsigned ? (int64_t)ullv : (int64_t)llv;
                    wasm_mem_write(runtime, dst, &v64, 8);
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
                    if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                    uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                    int32_t val = is_unsigned ? (int32_t)uv : iv;
                    if (lmod == 2) { // hh → 1 byte
                        if (!wasm_mem_check(runtime, dst, 1)) return matches;
                        wasm_mem_write8(runtime, dst, (uint8_t)val);
                    } else if (lmod == 1) { // h → 2 bytes
                        if (!wasm_mem_check(runtime, dst, 2)) return matches;
                        int16_t h = (int16_t)val;
                        wasm_mem_write(runtime, dst, &h, 2);
                    } else { // none/l → 4 bytes
                        if (!wasm_mem_check(runtime, dst, 4)) return matches;
                        if (is_unsigned)
                            wasm_mem_write(runtime, dst, &uv, 4);
                        else
                            wasm_mem_write(runtime, dst, &iv, 4);
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
                    if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                    uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                    if (!wasm_mem_check(runtime, dst, 8)) return matches;
                    wasm_mem_write(runtime, dst, &dv, 8);
                    matches++;
                }
            } else { // default → float (4 bytes)
                float fv = 0;
                snprintf(sfmt, sizeof(sfmt), "%%%sf%%n", wstr);
                if (sscanf(str_buf + spos, sfmt, &fv, &consumed) < 1) return matches;
                spos += consumed;
                if (!suppress) {
                    if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                    uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                    if (!wasm_mem_check(runtime, dst, 4)) return matches;
                    wasm_mem_write(runtime, dst, &fv, 4);
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
                if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                int slen = strlen(sbuf) + 1;
                if (!wasm_mem_check(runtime, dst, (size_t)slen)) return matches;
                wasm_mem_write(runtime, dst, sbuf, slen);
                matches++;
            }
            break;
        }

        case 'c': {
            if (str_buf[spos] == '\0') return matches;
            if (!suppress) {
                if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                if (!wasm_mem_check(runtime, dst, 1)) return matches;
                wasm_mem_write8(runtime, dst, (uint8_t)str_buf[spos]);
                matches++;
            }
            spos++;
            break;
        }

        case 'n': {
            if (!suppress) {
                if (!wasm_mem_check(runtime, aoff, 4)) return matches;
                uint32_t dst; wasm_mem_read(runtime, aoff, &dst, 4); aoff += 4;
                if (!wasm_mem_check(runtime, dst, 4)) return matches;
                int32_t pos = spos;
                wasm_mem_write(runtime, dst, &pos, 4);
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
