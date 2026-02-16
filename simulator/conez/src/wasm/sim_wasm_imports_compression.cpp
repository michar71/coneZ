#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "sim_config.h"
#include "inflate_util.h"
#include "m3_env.h"

#include <cstdio>
#include <cstring>
#include <string>

static bool valid_path(const char *p)
{
    if (!p || p[0] != '/') return false;
    if (strstr(p, "..")) return false;
    if (strcmp(p, "/config.ini") == 0) return false;
    return true;
}

static std::string sandbox(const char *path)
{
    return simConfig().sandbox_path + path;
}

static bool get_path(IM3Runtime runtime, int32_t ptr, int32_t len, char *out, int out_size)
{
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)ptr + len > mem_size || len <= 0 || len >= out_size)
        return false;
    memcpy(out, mem + ptr, len);
    out[len] = '\0';
    return true;
}

// i32 inflate_file(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_len) -> size or -1
m3ApiRawFunction(m3_inflate_file)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_len);

    char src_path[256], dst_path[256];
    if (!get_path(runtime, src_ptr, src_len, src_path, sizeof(src_path))) m3ApiReturn(-1);
    if (!get_path(runtime, dst_ptr, dst_len, dst_path, sizeof(dst_path))) m3ApiReturn(-1);
    if (!valid_path(src_path) || !valid_path(dst_path)) m3ApiReturn(-1);

    std::string src_full = sandbox(src_path);
    FILE *f = fopen(src_full.c_str(), "rb");
    if (!f) m3ApiReturn(-1);
    fseek(f, 0, SEEK_END);
    long in_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (in_size <= 0) { fclose(f); m3ApiReturn(-1); }

    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { fclose(f); m3ApiReturn(-1); }
    fread(in_buf, 1, in_size, f);
    fclose(f);

    size_t out_max = (size_t)in_size * 10;
    if (out_max > 256 * 1024) out_max = 256 * 1024;
    if (out_max < 4096) out_max = 4096;
    uint8_t *out_buf = (uint8_t *)malloc(out_max);
    if (!out_buf) { free(in_buf); m3ApiReturn(-1); }

    int result = inflate_buf(in_buf, in_size, out_buf, out_max);
    free(in_buf);

    if (result < 0) { free(out_buf); m3ApiReturn(-1); }

    std::string dst_full = sandbox(dst_path);
    FILE *out = fopen(dst_full.c_str(), "wb");
    if (!out) { free(out_buf); m3ApiReturn(-1); }
    fwrite(out_buf, 1, result, out);
    fclose(out);
    free(out_buf);

    m3ApiReturn(result);
}

// i32 inflate_file_to_mem(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_max) -> size or -1
m3ApiRawFunction(m3_inflate_file_to_mem)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_max);

    if (dst_max <= 0) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || (uint32_t)dst_ptr + dst_max > mem_size) m3ApiReturn(-1);

    char src_path[256];
    if (!get_path(runtime, src_ptr, src_len, src_path, sizeof(src_path))) m3ApiReturn(-1);
    if (!valid_path(src_path)) m3ApiReturn(-1);

    std::string src_full = sandbox(src_path);
    FILE *f = fopen(src_full.c_str(), "rb");
    if (!f) m3ApiReturn(-1);
    fseek(f, 0, SEEK_END);
    long in_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (in_size <= 0) { fclose(f); m3ApiReturn(-1); }

    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { fclose(f); m3ApiReturn(-1); }
    fread(in_buf, 1, in_size, f);
    fclose(f);

    int result = inflate_buf(in_buf, in_size, mem_base + dst_ptr, dst_max);
    free(in_buf);

    m3ApiReturn(result);
}

// i32 inflate_mem(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_max) -> size or -1
m3ApiRawFunction(m3_inflate_mem)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_max);

    if (src_len <= 0 || dst_max <= 0) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base) m3ApiReturn(-1);
    if ((uint32_t)src_ptr + src_len > mem_size) m3ApiReturn(-1);
    if ((uint32_t)dst_ptr + dst_max > mem_size) m3ApiReturn(-1);

    int result = inflate_buf(mem_base + src_ptr, src_len, mem_base + dst_ptr, dst_max);
    m3ApiReturn(result);
}

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_compression_imports(IM3Module module)
{
    M3Result r;
    LINK("inflate_file",        "i(iiii)", m3_inflate_file)
    LINK("inflate_file_to_mem", "i(iiii)", m3_inflate_file_to_mem)
    LINK("inflate_mem",         "i(iiii)", m3_inflate_mem)
    return m3Err_none;
}

#undef LINK
