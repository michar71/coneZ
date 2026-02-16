#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "main.h"
#include "inflate.h"
#include "psram.h"
#include "FS.h"
#include <LittleFS.h>
#include <stdlib.h>
#include <string.h>

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

// Copy from DRAM src buffer into WASM memory at (mem_base + offset).
// Routes through psram_write() if WASM memory is in unmapped PSRAM.
static void copy_to_wasm(uint8_t *mem_base, uint32_t offset, const uint8_t *src, size_t len)
{
    uint8_t *dst = mem_base + offset;
    if (IS_ADDRESS_MAPPED(dst))
        memcpy(dst, src, len);
    else
        psram_write((uint32_t)(uintptr_t)dst, src, len);
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
    File f = LittleFS.open(src_path, "r");
    if (!f) m3ApiReturn(-1);
    size_t in_size = f.size();
    if (in_size == 0) { f.close(); m3ApiReturn(-1); }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { f.close(); m3ApiReturn(-1); }
    f.read(in_buf, in_size);
    f.close();

    // Allocate output buffer
    size_t out_max = in_size * 10;
    if (out_max > 256 * 1024) out_max = 256 * 1024;
    if (out_max < 4096) out_max = 4096;
    uint8_t *out_buf = (uint8_t *)malloc(out_max);
    if (!out_buf) { free(in_buf); m3ApiReturn(-1); }

    int result = inflate_buf(in_buf, in_size, out_buf, out_max);
    free(in_buf);

    if (result < 0) { free(out_buf); m3ApiReturn(-1); }

    // Write output file
    File out = LittleFS.open(dst_path, FILE_WRITE);
    if (!out) { free(out_buf); m3ApiReturn(-1); }
    out.write(out_buf, result);
    out.close();
    free(out_buf);

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
    File f = LittleFS.open(src_path, "r");
    if (!f) m3ApiReturn(-1);
    size_t in_size = f.size();
    if (in_size == 0) { f.close(); m3ApiReturn(-1); }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) { f.close(); m3ApiReturn(-1); }
    f.read(in_buf, in_size);
    f.close();

    // Decompress to temp DRAM buffer, then copy to WASM memory
    uint8_t *tmp = (uint8_t *)malloc(dst_max);
    if (!tmp) { free(in_buf); m3ApiReturn(-1); }

    int result = inflate_buf(in_buf, in_size, tmp, dst_max);
    free(in_buf);

    if (result > 0)
        copy_to_wasm(mem_base, (uint32_t)dst_ptr, tmp, result);

    free(tmp);
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

    // Copy compressed data from WASM memory to DRAM
    uint8_t *in_buf = (uint8_t *)malloc(src_len);
    if (!in_buf) m3ApiReturn(-1);
    copy_from_wasm(mem_base, (uint32_t)src_ptr, in_buf, src_len);

    // Decompress to temp DRAM buffer
    uint8_t *tmp = (uint8_t *)malloc(dst_max);
    if (!tmp) { free(in_buf); m3ApiReturn(-1); }

    int result = inflate_buf(in_buf, src_len, tmp, dst_max);
    free(in_buf);

    if (result > 0)
        copy_to_wasm(mem_base, (uint32_t)dst_ptr, tmp, result);

    free(tmp);
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
