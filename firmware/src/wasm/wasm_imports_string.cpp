#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "printManager.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// ---- String Pool ----
// Host-managed free-list allocator operating on WASM linear memory.
// Pool region: 0x8000 .. 0xF000 (28KB)

#define STR_POOL_START  0x8000
#define STR_POOL_END    0xF000
#define STR_MAX_ALLOCS  128

struct StrAlloc {
    uint32_t offset;
    uint32_t size;
    bool in_use;
};

static StrAlloc str_allocs[STR_MAX_ALLOCS];
static int str_nallocs = 0;
static uint32_t str_bump = STR_POOL_START;

// Bounded strlen in WASM memory
int wasm_strlen(const uint8_t *mem, uint32_t mem_size, uint32_t ptr)
{
    if (ptr == 0 || ptr >= mem_size) return 0;
    int len = 0;
    while (ptr + len < mem_size && mem[ptr + len] != 0 && len < 4096)
        len++;
    return len;
}

uint32_t pool_alloc(IM3Runtime runtime, int size)
{
    if (size <= 0) size = 1;
    size = (size + 3) & ~3;  // 4-byte align

    // First-fit scan of freed blocks
    for (int i = 0; i < str_nallocs; i++) {
        if (!str_allocs[i].in_use && str_allocs[i].size >= (uint32_t)size) {
            str_allocs[i].in_use = true;
            // Zero the memory
            uint32_t mem_size = m3_GetMemorySize(runtime);
            uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
            if (mem && str_allocs[i].offset + size <= mem_size)
                memset(mem + str_allocs[i].offset, 0, size);
            return str_allocs[i].offset;
        }
    }

    // Bump allocate
    if (str_bump + size > STR_POOL_END) return 0;  // out of pool
    if (str_nallocs >= STR_MAX_ALLOCS) return 0;    // too many allocs

    uint32_t off = str_bump;
    str_bump += size;

    str_allocs[str_nallocs].offset = off;
    str_allocs[str_nallocs].size = size;
    str_allocs[str_nallocs].in_use = true;
    str_nallocs++;

    // Zero the memory
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (mem && off + size <= mem_size)
        memset(mem + off, 0, size);

    return off;
}

static void pool_free(uint32_t ptr)
{
    if (ptr < STR_POOL_START || ptr >= STR_POOL_END) return;  // outside pool (constant or null)
    for (int i = 0; i < str_nallocs; i++) {
        if (str_allocs[i].offset == ptr && str_allocs[i].in_use) {
            str_allocs[i].in_use = false;
            // Shrink bump if freeing top allocation
            if (ptr + str_allocs[i].size == str_bump) {
                str_bump = ptr;
                str_nallocs--;
            }
            return;
        }
    }
}

static uint32_t pool_size(uint32_t ptr)
{
    for (int i = 0; i < str_nallocs; i++) {
        if (str_allocs[i].offset == ptr && str_allocs[i].in_use)
            return str_allocs[i].size;
    }
    return 0;
}

static uint32_t pool_realloc(IM3Runtime runtime, uint32_t ptr, int size)
{
    if (ptr == 0) return pool_alloc(runtime, size);
    if (size <= 0) {
        pool_free(ptr);
        return 0;
    }

    size = (size + 3) & ~3;
    uint32_t old_size = pool_size(ptr);
    if (old_size == 0) return 0;
    if (old_size >= (uint32_t)size) return ptr;

    uint32_t nptr = pool_alloc(runtime, size);
    if (nptr == 0) return 0;

    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || ptr + old_size > mem_size || nptr + old_size > mem_size) {
        pool_free(nptr);
        return 0;
    }
    memcpy(mem + nptr, mem + ptr, old_size);
    pool_free(ptr);
    return nptr;
}

void wasm_string_pool_reset(void)
{
    str_nallocs = 0;
    str_bump = STR_POOL_START;
}


// ---- Host imports ----

// i32 str_alloc(i32 size) -> pool pointer or 0
m3ApiRawFunction(m3_str_alloc)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_alloc(runtime, size));
}

// void str_free(i32 ptr)
m3ApiRawFunction(m3_str_free)
{
    m3ApiGetArg(int32_t, ptr);
    pool_free((uint32_t)ptr);
    m3ApiSuccess();
}

// i32 malloc(i32 size) -> pool pointer or 0
m3ApiRawFunction(m3_malloc)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_alloc(runtime, size));
}

// void free(i32 ptr)
m3ApiRawFunction(m3_free)
{
    m3ApiGetArg(int32_t, ptr);
    pool_free((uint32_t)ptr);
    m3ApiSuccess();
}

// i32 calloc(i32 nmemb, i32 size) -> pool pointer or 0 (pool_alloc already zeroes)
m3ApiRawFunction(m3_calloc)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, nmemb);
    m3ApiGetArg(int32_t, size);
    if (nmemb <= 0 || size <= 0) m3ApiReturn(0);
    int64_t total = (int64_t)nmemb * (int64_t)size;
    if (total <= 0 || total > 0x7FFFFFFF) m3ApiReturn(0);
    m3ApiReturn((int32_t)pool_alloc(runtime, (int)total));
}

// i32 realloc(i32 ptr, i32 size) -> pool pointer or 0
m3ApiRawFunction(m3_realloc)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_realloc(runtime, (uint32_t)ptr, size));
}

// i32 str_len(i32 ptr) -> length
m3ApiRawFunction(m3_str_len)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) m3ApiReturn(0);
    m3ApiReturn(wasm_strlen(mem, mem_size, (uint32_t)ptr));
}

// i32 str_copy(i32 src) -> new pool string
m3ApiRawFunction(m3_str_copy)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int len = wasm_strlen(mem, mem_size, (uint32_t)src);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (dst == 0) m3ApiReturn(0);
    // Re-fetch mem after alloc (shouldn't change but be safe)
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src, len);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_concat(i32 a, i32 b) -> new pool string
m3ApiRawFunction(m3_str_concat)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) m3ApiReturn(0);
    int la = wasm_strlen(mem, mem_size, (uint32_t)a);
    int lb = wasm_strlen(mem, mem_size, (uint32_t)b);
    uint32_t dst = pool_alloc(runtime, la + lb + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)a, la);
    memcpy(mem + dst + la, mem + (uint32_t)b, lb);
    mem[dst + la + lb] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_cmp(i32 a, i32 b) -> <0, 0, >0 (like strcmp)
m3ApiRawFunction(m3_str_cmp)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) m3ApiReturn(0);
    const char *sa = (a && (uint32_t)a < mem_size) ? (const char *)(mem + (uint32_t)a) : "";
    const char *sb = (b && (uint32_t)b < mem_size) ? (const char *)(mem + (uint32_t)b) : "";
    m3ApiReturn(strcmp(sa, sb));
}

// i32 str_mid(i32 src, i32 start, i32 len) -> new pool string
// start is 1-based (BASIC convention)
m3ApiRawFunction(m3_str_mid)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, start);
    m3ApiGetArg(int32_t, count);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    int s = start - 1;  // convert to 0-based
    if (s < 0) s = 0;
    if (s >= slen) { /* empty result */
        uint32_t dst = pool_alloc(runtime, 1);
        m3ApiReturn((int32_t)dst);
    }
    int n = count;
    if (n < 0) n = 0;
    if (s + n > slen) n = slen - s;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src + s, n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_left(i32 src, i32 n) -> new pool string
m3ApiRawFunction(m3_str_left)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, n);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    if (n < 0) n = 0;
    if (n > slen) n = slen;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src, n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_right(i32 src, i32 n) -> new pool string
m3ApiRawFunction(m3_str_right)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, n);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    if (n < 0) n = 0;
    if (n > slen) n = slen;
    int s = slen - n;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src + s, n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_chr(i32 code) -> new 1-char pool string
m3ApiRawFunction(m3_str_chr)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, code);
    uint32_t dst = pool_alloc(runtime, 4);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    mem[dst] = (uint8_t)(code & 0xFF);
    mem[dst + 1] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_asc(i32 ptr) -> ASCII code of first char (0 if empty)
m3ApiRawFunction(m3_str_asc)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || ptr == 0 || (uint32_t)ptr >= mem_size) m3ApiReturn(0);
    m3ApiReturn((int32_t)mem[(uint32_t)ptr]);
}

// i32 str_from_int(i32 val) -> new pool string
m3ApiRawFunction(m3_str_from_int)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int n = snprintf(buf, sizeof(buf), "%d", val);
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, buf, n + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 str_from_i64(i64 val) -> new pool string
m3ApiRawFunction(m3_str_from_i64)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int64_t, val);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)val);
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, buf, n + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 str_from_float(f32 val) -> new pool string
m3ApiRawFunction(m3_str_from_float)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(float, val);
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "%g", val);
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, buf, n + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 str_to_int(i32 ptr) -> integer value
m3ApiRawFunction(m3_str_to_int)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || ptr == 0 || (uint32_t)ptr >= mem_size) m3ApiReturn(0);
    m3ApiReturn((int32_t)strtol((const char *)(mem + (uint32_t)ptr), NULL, 0));
}

// i64 str_to_i64(i32 ptr) -> integer value
m3ApiRawFunction(m3_str_to_i64)
{
    m3ApiReturnType(int64_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || ptr == 0 || (uint32_t)ptr >= mem_size) m3ApiReturn((int64_t)0);
    m3ApiReturn((int64_t)strtoll((const char *)(mem + (uint32_t)ptr), NULL, 0));
}

// f32 str_to_float(i32 ptr) -> float value
m3ApiRawFunction(m3_str_to_float)
{
    m3ApiReturnType(float);
    m3ApiGetArg(int32_t, ptr);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || ptr == 0 || (uint32_t)ptr >= mem_size) m3ApiReturn(0.0f);
    m3ApiReturn(strtof((const char *)(mem + (uint32_t)ptr), NULL));
}

// i32 str_upper(i32 src) -> new pool string (uppercased)
m3ApiRawFunction(m3_str_upper)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    uint32_t dst = pool_alloc(runtime, slen + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    for (int i = 0; i < slen; i++)
        mem[dst + i] = toupper(mem[(uint32_t)src + i]);
    mem[dst + slen] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_lower(i32 src) -> new pool string (lowercased)
m3ApiRawFunction(m3_str_lower)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    uint32_t dst = pool_alloc(runtime, slen + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    for (int i = 0; i < slen; i++)
        mem[dst + i] = tolower(mem[(uint32_t)src + i]);
    mem[dst + slen] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 str_instr(i32 haystack, i32 needle, i32 start) -> 1-based position or 0
// start is 1-based
m3ApiRawFunction(m3_str_instr)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, haystack);
    m3ApiGetArg(int32_t, needle);
    m3ApiGetArg(int32_t, start);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || haystack == 0 || needle == 0) m3ApiReturn(0);
    const char *h = (const char *)(mem + (uint32_t)haystack);
    const char *n = (const char *)(mem + (uint32_t)needle);
    int hlen = wasm_strlen(mem, mem_size, (uint32_t)haystack);
    int s = start - 1;  // convert to 0-based
    if (s < 0) s = 0;
    if (s >= hlen) m3ApiReturn(0);
    const char *found = strstr(h + s, n);
    if (!found) m3ApiReturn(0);
    m3ApiReturn((int32_t)(found - h) + 1);  // 1-based result
}

// i32 basic_str_trim(i32 src) -> new pool string (whitespace trimmed both sides)
m3ApiRawFunction(m3_str_trim)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    const uint8_t *p = mem + (uint32_t)src;
    int start = 0, end = slen;
    while (start < end && isspace(p[start])) start++;
    while (end > start && isspace(p[end - 1])) end--;
    int n = end - start;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src + start, n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_ltrim(i32 src) -> new pool string (leading whitespace trimmed)
m3ApiRawFunction(m3_str_ltrim)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    const uint8_t *p = mem + (uint32_t)src;
    int start = 0;
    while (start < slen && isspace(p[start])) start++;
    int n = slen - start;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src + start, n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_rtrim(i32 src) -> new pool string (trailing whitespace trimmed)
m3ApiRawFunction(m3_str_rtrim)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || src == 0) m3ApiReturn(0);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    const uint8_t *p = mem + (uint32_t)src;
    int end = slen;
    while (end > 0 && isspace(p[end - 1])) end--;
    uint32_t dst = pool_alloc(runtime, end + 1);
    if (dst == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, mem + (uint32_t)src, end);
    mem[dst + end] = 0;
    m3ApiReturn((int32_t)dst);
}


// i32 basic_str_repeat(i32 n, i32 char_code) -> new pool string of n copies of char
m3ApiRawFunction(m3_str_repeat)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, n);
    m3ApiGetArg(int32_t, char_code);
    if (n < 0) n = 0;
    if (n > 4096) n = 4096;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memset(mem + dst, (uint8_t)(char_code & 0xFF), n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_space(i32 n) -> new pool string of n spaces
m3ApiRawFunction(m3_str_space)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, n);
    if (n < 0) n = 0;
    if (n > 4096) n = 4096;
    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memset(mem + dst, ' ', n);
    mem[dst + n] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_hex(i32 val) -> new pool string with hex representation
m3ApiRawFunction(m3_str_hex)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%X", (unsigned int)val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_oct(i32 val) -> new pool string with octal representation
m3ApiRawFunction(m3_str_oct)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%o", (unsigned int)val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}


// i32 str_mid_assign(i32 dst, i32 start, i32 count, i32 src) -> i32
// Returns NEW pool string = dst with chars [start..start+n-1] replaced by src
// start is 1-based (BASIC convention)
m3ApiRawFunction(m3_str_mid_assign)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, dst);
    m3ApiGetArg(int32_t, start);
    m3ApiGetArg(int32_t, count);
    m3ApiGetArg(int32_t, src);
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || dst == 0) m3ApiReturn(0);
    int dlen = wasm_strlen(mem, mem_size, (uint32_t)dst);
    int slen = wasm_strlen(mem, mem_size, (uint32_t)src);
    int s = start - 1;  // convert to 0-based
    if (s < 0) s = 0;
    if (s >= dlen) {
        // start beyond string — return copy of original
        uint32_t r = pool_alloc(runtime, dlen + 1);
        if (r == 0) m3ApiReturn(0);
        mem = m3_GetMemory(runtime, &mem_size, 0);
        memcpy(mem + r, mem + (uint32_t)dst, dlen);
        mem[r + dlen] = 0;
        m3ApiReturn((int32_t)r);
    }
    int n = count;
    if (n < 0) n = 0;
    if (n > slen) n = slen;          // replace at most LEN(src) chars
    if (s + n > dlen) n = dlen - s;  // don't extend past original length
    // Allocate new string same length as dst
    uint32_t r = pool_alloc(runtime, dlen + 1);
    if (r == 0) m3ApiReturn(0);
    mem = m3_GetMemory(runtime, &mem_size, 0);
    memcpy(mem + r, mem + (uint32_t)dst, dlen);  // copy original
    memcpy(mem + r + s, mem + (uint32_t)src, n);  // overlay replacement
    mem[r + dlen] = 0;
    m3ApiReturn((int32_t)r);
}


// ---- Link string imports ----

M3Result link_string_imports(IM3Module module)
{
    M3Result result;

#define LINK(name, sig, fn) \
    result = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (result && result != m3Err_functionLookupFailed) return result;

    LINK("basic_str_alloc",      "i(i)",   m3_str_alloc);
    LINK("basic_str_free",       "v(i)",   m3_str_free);
    LINK("malloc",               "i(i)",   m3_malloc);
    LINK("free",                 "v(i)",   m3_free);
    LINK("calloc",               "i(ii)",  m3_calloc);
    LINK("realloc",              "i(ii)",  m3_realloc);
    LINK("basic_str_len",        "i(i)",   m3_str_len);
    LINK("basic_str_copy",       "i(i)",   m3_str_copy);
    LINK("basic_str_concat",     "i(ii)",  m3_str_concat);
    LINK("basic_str_cmp",        "i(ii)",  m3_str_cmp);
    LINK("basic_str_mid",        "i(iii)", m3_str_mid);
    LINK("basic_str_left",       "i(ii)",  m3_str_left);
    LINK("basic_str_right",      "i(ii)",  m3_str_right);
    LINK("basic_str_chr",        "i(i)",   m3_str_chr);
    LINK("basic_str_asc",        "i(i)",   m3_str_asc);
    LINK("basic_str_from_int",   "i(i)",   m3_str_from_int);
    LINK("basic_str_from_i64",   "i(I)",   m3_str_from_i64);
    LINK("basic_str_from_float", "i(f)",   m3_str_from_float);
    LINK("basic_str_to_int",     "i(i)",   m3_str_to_int);
    LINK("basic_str_to_i64",     "I(i)",   m3_str_to_i64);
    LINK("basic_str_to_float",   "f(i)",   m3_str_to_float);
    LINK("basic_str_upper",      "i(i)",   m3_str_upper);
    LINK("basic_str_lower",      "i(i)",   m3_str_lower);
    LINK("basic_str_instr",      "i(iii)", m3_str_instr);
    LINK("basic_str_trim",       "i(i)",   m3_str_trim);
    LINK("basic_str_ltrim",      "i(i)",   m3_str_ltrim);
    LINK("basic_str_rtrim",      "i(i)",   m3_str_rtrim);
    LINK("basic_str_repeat",     "i(ii)",  m3_str_repeat);
    LINK("basic_str_space",      "i(i)",   m3_str_space);
    LINK("basic_str_hex",        "i(i)",   m3_str_hex);
    LINK("basic_str_oct",        "i(i)",   m3_str_oct);
    LINK("basic_str_mid_assign", "i(iiii)",m3_str_mid_assign);

#undef LINK

    return m3Err_none;
}

#endif // INCLUDE_WASM
