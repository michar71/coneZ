#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "main.h"
#include "inflate.h"
#include "psram.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WASM_MAX_PATH_LEN 128

// Path validation (same logic as wasm_imports_file.cpp)
static bool path_ok(const char *path, int len)
{
    if (len <= 0 || len >= WASM_MAX_PATH_LEN) return false;
    if (path[0] != '/') return false;
    for (int i = 0; i < len - 1; i++)
        if (path[i] == '.' && path[i+1] == '.') return false;
    if (len == 11 && memcmp(path, "/config.ini", 11) == 0) return false;
    return true;
}

static bool extract_path(IM3Runtime runtime, int32_t ptr, int32_t len, char *out)
{
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || len <= 0 || len >= WASM_MAX_PATH_LEN || (uint32_t)ptr + len > mem_size)
        return false;
    memcpy(out, mem + ptr, len);
    out[len] = '\0';
    return path_ok(out, len);
}

// Copy from WASM memory at (mem_base + offset) into DRAM dst buffer.
// Routes through psram_read() if WASM memory is in unmapped PSRAM.
static void copy_from_wasm(const uint8_t *mem_base, uint32_t offset, uint8_t *dst, size_t len)
{
    const uint8_t *src = mem_base + offset;
    if (IS_ADDRESS_MAPPED(src))
        memcpy(dst, src, len);
    else
        psram_read((uint32_t)(uintptr_t)src, dst, len);
}

// Streaming write callback for FILE*
static int file_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fwrite(data, 1, len, f) == len ? 0 : -1;
}

// Streaming write callback for WASM memory (PSRAM-safe)
struct wasm_mem_ctx {
    uint8_t *mem_base;
    uint32_t offset;
    size_t max;
    size_t written;
};

static int wasm_mem_write(const uint8_t *data, size_t len, void *ctx)
{
    struct wasm_mem_ctx *c = (struct wasm_mem_ctx *)ctx;
    if (c->written + len > c->max) return -1;
    uint8_t *dst = c->mem_base + c->offset + c->written;
    if (IS_ADDRESS_MAPPED(dst))
        memcpy(dst, data, len);
    else
        psram_write((uint32_t)(uintptr_t)dst, data, len);
    c->written += len;
    return 0;
}

// i32 inflate_file(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_len) -> decompressed size or -1
m3ApiRawFunction(m3_inflate_file)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_len);

    char src_path[WASM_MAX_PATH_LEN], dst_path[WASM_MAX_PATH_LEN];
    if (!extract_path(runtime, src_ptr, src_len, src_path)) m3ApiReturn(-1);
    if (!extract_path(runtime, dst_ptr, dst_len, dst_path)) m3ApiReturn(-1);

    // Read compressed file
    char src_fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(src_fpath, sizeof(src_fpath), src_path);
    FILE *f = fopen(src_fpath, "r");
    if (!f) m3ApiReturn(-1);
    size_t in_size = fsize(f);
    if (in_size == 0) { fclose(f); m3ApiReturn(-1); }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { fclose(f); m3ApiReturn(-1); }
    fread(in_buf, 1, in_size, f);
    fclose(f);

    // Stream decompressed chunks directly to output file
    char dst_fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(dst_fpath, sizeof(dst_fpath), dst_path);
    FILE *out = fopen(dst_fpath, "w");
    if (!out) { free(in_buf); m3ApiReturn(-1); }

    int result = inflate_stream(in_buf, in_size, file_write_cb, out);
    fclose(out);
    free(in_buf);

    if (result < 0) unlink(dst_fpath);
    m3ApiReturn(result);
}

// i32 inflate_file_to_mem(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_max) -> decompressed size or -1
m3ApiRawFunction(m3_inflate_file_to_mem)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_max);

    if (dst_max <= 0) m3ApiReturn(-1);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || (uint32_t)dst_ptr + dst_max > mem_size) m3ApiReturn(-1);

    char src_path[WASM_MAX_PATH_LEN];
    if (!extract_path(runtime, src_ptr, src_len, src_path)) m3ApiReturn(-1);

    // Read compressed file
    char src_fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(src_fpath, sizeof(src_fpath), src_path);
    FILE *f = fopen(src_fpath, "r");
    if (!f) m3ApiReturn(-1);
    size_t in_size = fsize(f);
    if (in_size == 0) { fclose(f); m3ApiReturn(-1); }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { fclose(f); m3ApiReturn(-1); }
    fread(in_buf, 1, in_size, f);
    fclose(f);

    // Stream decompressed chunks directly to WASM memory (PSRAM-safe)
    struct wasm_mem_ctx ctx = { mem_base, (uint32_t)dst_ptr, (size_t)dst_max, 0 };
    int result = inflate_stream(in_buf, in_size, wasm_mem_write, &ctx);
    free(in_buf);

    m3ApiReturn(result);
}

// i32 inflate_mem(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_max) -> decompressed size or -1
m3ApiRawFunction(m3_inflate_mem)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_max);

    if (src_len <= 0 || dst_max <= 0) m3ApiReturn(-1);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base) m3ApiReturn(-1);
    if ((uint32_t)src_ptr + src_len > mem_size) m3ApiReturn(-1);
    if ((uint32_t)dst_ptr + dst_max > mem_size) m3ApiReturn(-1);

    // Copy compressed data from WASM memory to DRAM (small — compressed size)
    uint8_t *in_buf = (uint8_t *)malloc(src_len);
    if (!in_buf) m3ApiReturn(-1);
    copy_from_wasm(mem_base, (uint32_t)src_ptr, in_buf, src_len);

    // Stream decompressed chunks directly to WASM memory (PSRAM-safe)
    struct wasm_mem_ctx ctx = { mem_base, (uint32_t)dst_ptr, (size_t)dst_max, 0 };
    int result = inflate_stream(in_buf, src_len, wasm_mem_write, &ctx);
    free(in_buf);

    m3ApiReturn(result);
}

M3Result link_compression_imports(IM3Module module)
{
    M3Result r;

    r = m3_LinkRawFunction(module, "env", "inflate_file", "i(iiii)", m3_inflate_file);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "inflate_file_to_mem", "i(iiii)", m3_inflate_file_to_mem);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "inflate_mem", "i(iiii)", m3_inflate_mem);
    if (r && r != m3Err_functionLookupFailed) return r;

    return m3Err_none;
}

#endif // INCLUDE_WASM
