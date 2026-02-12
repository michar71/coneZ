#include "sim_wasm_imports.h"
#include "m3_env.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <algorithm>

// ---- String pool allocator (mirrors firmware) ----
// Pool region in WASM linear memory: 0x8000..0xF000 (28KB)

#define STR_POOL_BASE  0x8000
#define STR_POOL_END   0xF000
#define STR_MAX_ALLOCS 128

struct StrAlloc {
    uint32_t offset;
    uint32_t size;
    bool in_use;
};

static StrAlloc allocs[STR_MAX_ALLOCS];
static int alloc_count = 0;
static uint32_t bump_ptr = STR_POOL_BASE;

void wasm_string_pool_reset()
{
    alloc_count = 0;
    bump_ptr = STR_POOL_BASE;
}

uint32_t pool_alloc(IM3Runtime runtime, int size)
{
    if (size <= 0) size = 1;
    size = (size + 3) & ~3; // 4-byte align

    // Try freed blocks first (first-fit)
    for (int i = 0; i < alloc_count; i++) {
        if (!allocs[i].in_use && allocs[i].size >= (uint32_t)size) {
            allocs[i].in_use = true;
            uint32_t ms = 0;
            uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
            if (mem && allocs[i].offset + (uint32_t)size <= ms)
                memset(mem + allocs[i].offset, 0, (size_t)size);
            return allocs[i].offset;
        }
    }

    // Bump allocate
    if (bump_ptr + size > STR_POOL_END) return 0; // OOM
    if (alloc_count >= STR_MAX_ALLOCS) return 0;

    uint32_t ptr = bump_ptr;
    bump_ptr += size;

    allocs[alloc_count].offset = ptr;
    allocs[alloc_count].size = size;
    allocs[alloc_count].in_use = true;
    alloc_count++;

    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (mem && ptr + (uint32_t)size <= ms)
        memset(mem + ptr, 0, (size_t)size);

    return ptr;
}

static void pool_free(uint32_t ptr)
{
    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].offset == ptr && allocs[i].in_use) {
            allocs[i].in_use = false;
            if (ptr + allocs[i].size == bump_ptr) {
                bump_ptr = ptr;
                alloc_count--;
            }
            return;
        }
    }
}

static uint32_t pool_size(uint32_t ptr)
{
    for (int i = 0; i < alloc_count; i++) {
        if (allocs[i].offset == ptr && allocs[i].in_use)
            return allocs[i].size;
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
    if (!nptr) return 0;
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem || ptr + old_size > ms || nptr + old_size > ms) {
        pool_free(nptr);
        return 0;
    }
    memcpy(mem + nptr, mem + ptr, old_size);
    pool_free(ptr);
    return nptr;
}

int wasm_strlen(const uint8_t *mem, uint32_t mem_size, uint32_t ptr)
{
    if (ptr >= mem_size) return 0;
    int len = 0;
    uint32_t max = mem_size - ptr;
    if (max > 4096) max = 4096;
    while ((uint32_t)len < max && mem[ptr + len]) len++;
    return len;
}

// Helper: get null-terminated string from WASM mem at pool ptr
static const char *pool_str(IM3Runtime runtime, uint32_t ptr, uint32_t *mem_size_out)
{
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (mem_size_out) *mem_size_out = ms;
    if (!mem || ptr >= ms) return nullptr;
    return (const char *)mem + ptr;
}

// ---- Import functions ----

// i32 basic_str_alloc(i32 size)
m3ApiRawFunction(m3_str_alloc) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_alloc(runtime, size));
}

// void basic_str_free(i32 ptr)
m3ApiRawFunction(m3_str_free) {
    m3ApiGetArg(int32_t, ptr);
    pool_free(ptr);
    m3ApiSuccess();
}

// i32 malloc(i32 size)
m3ApiRawFunction(m3_malloc) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_alloc(runtime, size));
}

// void free(i32 ptr)
m3ApiRawFunction(m3_free) {
    m3ApiGetArg(int32_t, ptr);
    pool_free((uint32_t)ptr);
    m3ApiSuccess();
}

// i32 calloc(i32 nmemb, i32 size)
m3ApiRawFunction(m3_calloc) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, nmemb);
    m3ApiGetArg(int32_t, size);
    if (nmemb <= 0 || size <= 0) m3ApiReturn(0);
    int64_t total = (int64_t)nmemb * (int64_t)size;
    if (total <= 0 || total > 0x7FFFFFFF) m3ApiReturn(0);
    m3ApiReturn((int32_t)pool_alloc(runtime, (int)total));
}

// i32 realloc(i32 ptr, i32 size)
m3ApiRawFunction(m3_realloc) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, size);
    m3ApiReturn((int32_t)pool_realloc(runtime, (uint32_t)ptr, size));
}

// i32 basic_str_len(i32 ptr)
m3ApiRawFunction(m3_str_len) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    m3ApiReturn(mem ? wasm_strlen(mem, ms, ptr) : 0);
}

// i32 basic_str_copy(i32 src) -> new alloc
m3ApiRawFunction(m3_str_copy) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0); // re-get after potential growth
    memcpy(mem + dst, mem + src, len + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_concat(i32 a, i32 b) -> new alloc
m3ApiRawFunction(m3_str_concat) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int la = wasm_strlen(mem, ms, a);
    int lb = wasm_strlen(mem, ms, b);
    uint32_t dst = pool_alloc(runtime, la + lb + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + a, la);
    memcpy(mem + dst + la, mem + b, lb);
    mem[dst + la + lb] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_cmp(i32 a, i32 b)
m3ApiRawFunction(m3_str_cmp) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, a);
    m3ApiGetArg(int32_t, b);
    uint32_t ms = 0;
    const char *sa = pool_str(runtime, a, &ms);
    const char *sb = pool_str(runtime, b, nullptr);
    if (!sa || !sb) { m3ApiReturn(0); }
    m3ApiReturn(strcmp(sa, sb));
}

// i32 basic_str_mid(i32 src, i32 start, i32 len) — 1-based
m3ApiRawFunction(m3_str_mid) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, start);
    m3ApiGetArg(int32_t, len);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int slen = wasm_strlen(mem, ms, src);
    int idx = start - 1; // 1-based to 0-based
    if (idx < 0) idx = 0;
    if (idx >= slen) { // return empty
        uint32_t dst = pool_alloc(runtime, 1);
        mem = m3_GetMemory(runtime, &ms, 0);
        if (dst) mem[dst] = 0;
        m3ApiReturn((int32_t)dst);
    }
    if (len < 0 || idx + len > slen) len = slen - idx;
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src + idx, len);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_left(i32 src, i32 len)
m3ApiRawFunction(m3_str_left) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, len);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int slen = wasm_strlen(mem, ms, src);
    if (len > slen) len = slen;
    if (len < 0) len = 0;
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src, len);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_right(i32 src, i32 len)
m3ApiRawFunction(m3_str_right) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, len);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int slen = wasm_strlen(mem, ms, src);
    if (len > slen) len = slen;
    if (len < 0) len = 0;
    int start = slen - len;
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src + start, len);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_chr(i32 code)
m3ApiRawFunction(m3_str_chr) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, code);
    uint32_t dst = pool_alloc(runtime, 2);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    mem[dst] = (uint8_t)code;
    mem[dst + 1] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_asc(i32 ptr)
m3ApiRawFunction(m3_str_asc) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem || ptr >= (int32_t)ms) { m3ApiReturn(0); }
    m3ApiReturn((int32_t)mem[ptr]);
}

// i32 basic_str_instr(i32 start, i32 haystack, i32 needle) — 1-based
m3ApiRawFunction(m3_str_instr) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, start);
    m3ApiGetArg(int32_t, haystack);
    m3ApiGetArg(int32_t, needle);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    const char *h = (const char *)mem + haystack;
    const char *n = (const char *)mem + needle;
    int hlen = wasm_strlen(mem, ms, haystack);
    int nlen = wasm_strlen(mem, ms, needle);
    int idx = start - 1;
    if (idx < 0) idx = 0;
    if (nlen == 0) { m3ApiReturn(idx + 1); }
    for (int i = idx; i <= hlen - nlen; i++) {
        if (memcmp(h + i, n, nlen) == 0) { m3ApiReturn(i + 1); }
    }
    m3ApiReturn(0);
}

// i32 basic_str_mid_assign(i32 dst, i32 start, i32 len, i32 src)
m3ApiRawFunction(m3_str_mid_assign) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, dst);
    m3ApiGetArg(int32_t, start);
    m3ApiGetArg(int32_t, len);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(dst); }
    int dlen = wasm_strlen(mem, ms, dst);
    int slen = wasm_strlen(mem, ms, src);
    int idx = start - 1;
    if (idx < 0 || idx >= dlen) { m3ApiReturn(dst); }
    int copy = len;
    if (copy > slen) copy = slen;
    if (idx + copy > dlen) copy = dlen - idx;
    memcpy(mem + dst + idx, mem + src, copy);
    m3ApiReturn(dst);
}

// Type conversions

// i32 basic_str_from_int(i32 val)
m3ApiRawFunction(m3_str_from_int) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_from_float(f32 val)
m3ApiRawFunction(m3_str_from_float) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(float, val);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%g", (double)val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_to_int(i32 ptr)
m3ApiRawFunction(m3_str_to_int) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, ptr);
    uint32_t ms = 0;
    const char *s = pool_str(runtime, ptr, &ms);
    m3ApiReturn(s ? atoi(s) : 0);
}

// f32 basic_str_to_float(i32 ptr)
m3ApiRawFunction(m3_str_to_float) {
    m3ApiReturnType(float);
    m3ApiGetArg(int32_t, ptr);
    uint32_t ms = 0;
    const char *s = pool_str(runtime, ptr, &ms);
    m3ApiReturn(s ? (float)atof(s) : 0.0f);
}

// i32 basic_str_hex(i32 val)
m3ApiRawFunction(m3_str_hex) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%X", (unsigned)val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_oct(i32 val)
m3ApiRawFunction(m3_str_oct) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, val);
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%o", (unsigned)val);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, buf, len + 1);
    m3ApiReturn((int32_t)dst);
}

// Case conversion

// i32 basic_str_upper(i32 src)
m3ApiRawFunction(m3_str_upper) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    for (int i = 0; i < len; i++) mem[dst + i] = toupper(mem[src + i]);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_lower(i32 src)
m3ApiRawFunction(m3_str_lower) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    uint32_t dst = pool_alloc(runtime, len + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    for (int i = 0; i < len; i++) mem[dst + i] = tolower(mem[src + i]);
    mem[dst + len] = 0;
    m3ApiReturn((int32_t)dst);
}

// Trimming

// i32 basic_str_trim(i32 src)
m3ApiRawFunction(m3_str_trim) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    int start = 0, end = len;
    while (start < end && isspace(mem[src + start])) start++;
    while (end > start && isspace(mem[src + end - 1])) end--;
    int nlen = end - start;
    uint32_t dst = pool_alloc(runtime, nlen + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src + start, nlen);
    mem[dst + nlen] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_ltrim(i32 src)
m3ApiRawFunction(m3_str_ltrim) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    int start = 0;
    while (start < len && isspace(mem[src + start])) start++;
    int nlen = len - start;
    uint32_t dst = pool_alloc(runtime, nlen + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src + start, nlen);
    mem[dst + nlen] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_rtrim(i32 src)
m3ApiRawFunction(m3_str_rtrim) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem) { m3ApiReturn(0); }
    int len = wasm_strlen(mem, ms, src);
    int end = len;
    while (end > 0 && isspace(mem[src + end - 1])) end--;
    uint32_t dst = pool_alloc(runtime, end + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    memcpy(mem + dst, mem + src, end);
    mem[dst + end] = 0;
    m3ApiReturn((int32_t)dst);
}

// Padding

// i32 basic_str_repeat(i32 src, i32 count)
m3ApiRawFunction(m3_str_repeat) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src);
    m3ApiGetArg(int32_t, count);
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    if (!mem || count <= 0) { m3ApiReturn(0); }
    int slen = wasm_strlen(mem, ms, src);
    int total = slen * count;
    if (total > 4096) total = 4096;
    uint32_t dst = pool_alloc(runtime, total + 1);
    if (!dst) { m3ApiReturn(0); }
    mem = m3_GetMemory(runtime, &ms, 0);
    for (int i = 0; i < count && i * slen < total; i++)
        memcpy(mem + dst + i * slen, mem + src, slen);
    mem[dst + total] = 0;
    m3ApiReturn((int32_t)dst);
}

// i32 basic_str_space(i32 count)
m3ApiRawFunction(m3_str_space) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, count);
    if (count < 0) count = 0;
    if (count > 4096) count = 4096;
    uint32_t dst = pool_alloc(runtime, count + 1);
    if (!dst) { m3ApiReturn(0); }
    uint32_t ms = 0;
    uint8_t *mem = m3_GetMemory(runtime, &ms, 0);
    memset(mem + dst, ' ', count);
    mem[dst + count] = 0;
    m3ApiReturn((int32_t)dst);
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_string_imports(IM3Module module)
{
    M3Result r;

    LINK("basic_str_alloc",  "i(i)",    m3_str_alloc)
    LINK("basic_str_free",   "v(i)",    m3_str_free)
    LINK("malloc",           "i(i)",    m3_malloc)
    LINK("free",             "v(i)",    m3_free)
    LINK("calloc",           "i(ii)",   m3_calloc)
    LINK("realloc",          "i(ii)",   m3_realloc)
    LINK("basic_str_len",    "i(i)",    m3_str_len)

    LINK("basic_str_copy",   "i(i)",    m3_str_copy)
    LINK("basic_str_concat", "i(ii)",   m3_str_concat)
    LINK("basic_str_cmp",    "i(ii)",   m3_str_cmp)
    LINK("basic_str_mid",    "i(iii)",  m3_str_mid)
    LINK("basic_str_left",   "i(ii)",   m3_str_left)
    LINK("basic_str_right",  "i(ii)",   m3_str_right)
    LINK("basic_str_chr",    "i(i)",    m3_str_chr)
    LINK("basic_str_asc",    "i(i)",    m3_str_asc)
    LINK("basic_str_instr",  "i(iii)",  m3_str_instr)
    LINK("basic_str_mid_assign", "i(iiii)", m3_str_mid_assign)

    LINK("basic_str_from_int",   "i(i)", m3_str_from_int)
    LINK("basic_str_from_float", "i(f)", m3_str_from_float)
    LINK("basic_str_to_int",     "i(i)", m3_str_to_int)
    LINK("basic_str_to_float",   "f(i)", m3_str_to_float)
    LINK("basic_str_hex",        "i(i)", m3_str_hex)
    LINK("basic_str_oct",        "i(i)", m3_str_oct)

    LINK("basic_str_upper", "i(i)", m3_str_upper)
    LINK("basic_str_lower", "i(i)", m3_str_lower)

    LINK("basic_str_trim",  "i(i)", m3_str_trim)
    LINK("basic_str_ltrim", "i(i)", m3_str_ltrim)
    LINK("basic_str_rtrim", "i(i)", m3_str_rtrim)

    LINK("basic_str_repeat", "i(ii)", m3_str_repeat)
    LINK("basic_str_space",  "i(i)",  m3_str_space)

    return m3Err_none;
}

#undef LINK
