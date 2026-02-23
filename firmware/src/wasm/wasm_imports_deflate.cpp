#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "main.h"
#include "deflate.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define WASM_MAX_PATH_LEN 128

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
    if (len <= 0 || len >= WASM_MAX_PATH_LEN) return false;
    if (!wasm_mem_check(runtime, (uint32_t)ptr, (size_t)len)) return false;
    wasm_mem_read(runtime, (uint32_t)ptr, out, (size_t)len);
    out[len] = '\0';
    return path_ok(out, len);
}

/* Streaming write callback for FILE* */
static int file_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    FILE *f = (FILE *)ctx;
    return fwrite(data, 1, len, f) == len ? 0 : -1;
}

/* Streaming write callback for WASM memory via wasm_mem_write helpers */
struct wasm_write_ctx {
    IM3Runtime runtime;
    uint32_t offset;
    size_t max;
    size_t written;
};

static int wasm_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    struct wasm_write_ctx *c = (struct wasm_write_ctx *)ctx;
    if (c->written + len > c->max) return -1;
    wasm_mem_write(c->runtime, c->offset + (uint32_t)c->written, data, len);
    c->written += len;
    return 0;
}

/* Default compression settings per board */
#ifdef BOARD_HAS_IMPROVISED_PSRAM
  #define DEF_WBITS  15
  #define DEF_MLEVEL 8
#else
  #define DEF_WBITS  13
  #define DEF_MLEVEL 6
#endif
#define DEF_LEVEL  6

// i32 deflate_file(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_len) -> compressed size or -1
m3ApiRawFunction(m3_deflate_file)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_len);

    char src_path[WASM_MAX_PATH_LEN], dst_path[WASM_MAX_PATH_LEN];
    if (!extract_path(runtime, src_ptr, src_len, src_path)) m3ApiReturn(-1);
    if (!extract_path(runtime, dst_ptr, dst_len, dst_path)) m3ApiReturn(-1);

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

    char dst_fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(dst_fpath, sizeof(dst_fpath), dst_path);
    FILE *out = fopen(dst_fpath, "w");
    if (!out) { free(in_buf); m3ApiReturn(-1); }

    int result = gzip_stream(in_buf, in_size, file_write_cb, out,
                             DEF_WBITS, DEF_MLEVEL, DEF_LEVEL);
    fclose(out);
    free(in_buf);

    if (result < 0) unlink(dst_fpath);
    m3ApiReturn(result);
}

// i32 deflate_mem_to_file(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_len) -> compressed size or -1
m3ApiRawFunction(m3_deflate_mem_to_file)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_len);

    if (src_len <= 0) m3ApiReturn(-1);
    if (!wasm_mem_check(runtime, (uint32_t)src_ptr, (size_t)src_len)) m3ApiReturn(-1);

    char dst_path[WASM_MAX_PATH_LEN];
    if (!extract_path(runtime, dst_ptr, dst_len, dst_path)) m3ApiReturn(-1);

    /* Copy source from WASM memory to DRAM */
    uint8_t *in_buf = (uint8_t *)malloc(src_len);
    if (!in_buf) m3ApiReturn(-1);
    wasm_mem_read(runtime, (uint32_t)src_ptr, in_buf, (size_t)src_len);

    char dst_fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(dst_fpath, sizeof(dst_fpath), dst_path);
    FILE *out = fopen(dst_fpath, "w");
    if (!out) { free(in_buf); m3ApiReturn(-1); }

    int result = gzip_stream(in_buf, src_len, file_write_cb, out,
                             DEF_WBITS, DEF_MLEVEL, DEF_LEVEL);
    fclose(out);
    free(in_buf);

    if (result < 0) unlink(dst_fpath);
    m3ApiReturn(result);
}

// i32 deflate_mem(i32 src_ptr, i32 src_len, i32 dst_ptr, i32 dst_max) -> compressed size or -1
m3ApiRawFunction(m3_deflate_mem)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, src_ptr);
    m3ApiGetArg(int32_t, src_len);
    m3ApiGetArg(int32_t, dst_ptr);
    m3ApiGetArg(int32_t, dst_max);

    if (src_len <= 0 || dst_max <= 0) m3ApiReturn(-1);
    if (!wasm_mem_check(runtime, (uint32_t)src_ptr, (size_t)src_len)) m3ApiReturn(-1);
    if (!wasm_mem_check(runtime, (uint32_t)dst_ptr, (size_t)dst_max)) m3ApiReturn(-1);

    /* Copy source from WASM memory to DRAM */
    uint8_t *in_buf = (uint8_t *)malloc(src_len);
    if (!in_buf) m3ApiReturn(-1);
    wasm_mem_read(runtime, (uint32_t)src_ptr, in_buf, (size_t)src_len);

    /* Stream compressed chunks to WASM memory via helpers */
    struct wasm_write_ctx ctx = { runtime, (uint32_t)dst_ptr, (size_t)dst_max, 0 };
    int result = gzip_stream(in_buf, src_len, wasm_write_cb, &ctx,
                             DEF_WBITS, DEF_MLEVEL, DEF_LEVEL);
    free(in_buf);

    m3ApiReturn(result);
}

M3Result link_deflate_imports(IM3Module module)
{
    M3Result r;

    r = m3_LinkRawFunction(module, "env", "deflate_file", "i(iiii)", m3_deflate_file);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "deflate_mem_to_file", "i(iiii)", m3_deflate_mem_to_file);
    if (r && r != m3Err_functionLookupFailed) return r;

    r = m3_LinkRawFunction(module, "env", "deflate_mem", "i(iiii)", m3_deflate_mem);
    if (r && r != m3Err_functionLookupFailed) return r;

    return m3Err_none;
}

#endif // INCLUDE_WASM
