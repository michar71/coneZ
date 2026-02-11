#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "sim_config.h"
#include "m3_env.h"

#include <cstdio>
#include <cstring>
#include <string>

// ---- Output functions ----

m3ApiRawFunction(m3_print_i32) {
    m3ApiGetArg(int32_t, val);
    auto *rt = currentRuntime();
    if (rt) rt->emitOutput(std::to_string(val) + "\n");
    m3ApiSuccess();
}

m3ApiRawFunction(m3_print_f32) {
    m3ApiGetArg(float, val);
    auto *rt = currentRuntime();
    if (rt) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g\n", (double)val);
        rt->emitOutput(buf);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_print_i64) {
    m3ApiGetArg(int64_t, val);
    auto *rt = currentRuntime();
    if (rt) rt->emitOutput(std::to_string(val) + "\n");
    m3ApiSuccess();
}

m3ApiRawFunction(m3_print_f64) {
    m3ApiGetArg(double, val);
    auto *rt = currentRuntime();
    if (rt) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g\n", val);
        rt->emitOutput(buf);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_print_str) {
    m3ApiGetArg(int32_t, ptr);
    m3ApiGetArg(int32_t, len);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)ptr + len > mem_size) { m3ApiSuccess(); }

    auto *rt = currentRuntime();
    if (rt) rt->emitOutput(std::string((char *)mem + ptr, len));
    m3ApiSuccess();
}

// ---- WASI fd_write (for printf from wasm-compiled C) ----

m3ApiRawFunction(m3_wasi_fd_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fd);
    m3ApiGetArg(int32_t, iovs_ptr);
    m3ApiGetArg(int32_t, iovs_len);
    m3ApiGetArg(int32_t, nwritten_ptr);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem) { m3ApiReturn(8); } // EBADF

    // Only stdout(1) and stderr(2)
    if (fd != 1 && fd != 2) { m3ApiReturn(8); }

    uint32_t total = 0;
    auto *rt = currentRuntime();

    for (int32_t i = 0; i < iovs_len; i++) {
        uint32_t iov_off = iovs_ptr + i * 8;
        if (iov_off + 8 > mem_size) break;

        uint32_t buf_ptr = *(uint32_t *)(mem + iov_off);
        uint32_t buf_len = *(uint32_t *)(mem + iov_off + 4);

        if (buf_ptr + buf_len > mem_size) buf_len = mem_size - buf_ptr;

        if (rt && buf_len > 0)
            rt->emitOutput(std::string((char *)mem + buf_ptr, buf_len));

        total += buf_len;
    }

    if (nwritten_ptr + 4 <= mem_size)
        *(uint32_t *)(mem + nwritten_ptr) = total;

    m3ApiReturn(0); // success
}

m3ApiRawFunction(m3_wasi_fd_seek) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_wasi_fd_close) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_wasi_proc_exit) {
    m3ApiGetArg(int32_t, code);
    (void)code;
    m3ApiTrap(m3Err_trapExit);
}

// ---- LUT (lookup tables — simplified: single in-memory table) ----

static int lut_data[4096];
static int lut_count = 0;

m3ApiRawFunction(m3_lut_load) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);

    // Try to load from sandbox directory
    auto &cfg = simConfig();
    char path[256];
    snprintf(path, sizeof(path), "%s/lut%d.csv", cfg.sandbox_path.c_str(), index);

    FILE *f = fopen(path, "r");
    if (!f) { lut_count = 0; m3ApiReturn(0); }

    lut_count = 0;
    int val;
    while (lut_count < 4096 && fscanf(f, "%d", &val) == 1) {
        lut_data[lut_count++] = val;
        // Skip comma or newline
        int c = fgetc(f);
        if (c != ',' && c != '\n' && c != EOF) ungetc(c, f);
    }
    fclose(f);
    m3ApiReturn(lut_count);
}

m3ApiRawFunction(m3_lut_get) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index >= lut_count) { m3ApiReturn(0); }
    m3ApiReturn(lut_data[index]);
}

m3ApiRawFunction(m3_lut_size) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(lut_count);
}

m3ApiRawFunction(m3_lut_set) {
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(int32_t, value);
    if (index >= 0 && index < lut_count)
        lut_data[index] = value;
    m3ApiSuccess();
}

m3ApiRawFunction(m3_lut_save) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);

    auto &cfg = simConfig();
    char path[256];
    snprintf(path, sizeof(path), "%s/lut%d.csv", cfg.sandbox_path.c_str(), index);

    FILE *f = fopen(path, "w");
    if (!f) { m3ApiReturn(0); }

    for (int i = 0; i < lut_count; i++) {
        if (i > 0) fprintf(f, ",");
        fprintf(f, "%d", lut_data[i]);
    }
    fprintf(f, "\n");
    fclose(f);
    m3ApiReturn(1);
}

m3ApiRawFunction(m3_lut_check) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);

    auto &cfg = simConfig();
    char path[256];
    snprintf(path, sizeof(path), "%s/lut%d.csv", cfg.sandbox_path.c_str(), index);

    FILE *f = fopen(path, "r");
    if (!f) { m3ApiReturn(-1); }

    int count = 0, val;
    while (count < 4096 && fscanf(f, "%d", &val) == 1) {
        count++;
        int c = fgetc(f);
        if (c != ',' && c != '\n' && c != EOF) ungetc(c, f);
    }
    fclose(f);
    m3ApiReturn(count);
}

// ---- Link ----

#define LINK(mod, name, sig, fn) \
    r = m3_LinkRawFunction(module, mod, name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_io_imports(IM3Module module)
{
    M3Result r;

    // Output
    LINK("env", "print_i32", "v(i)",  m3_print_i32)
    LINK("env", "print_f32", "v(f)",  m3_print_f32)
    LINK("env", "print_i64", "v(I)",  m3_print_i64)
    LINK("env", "print_f64", "v(F)",  m3_print_f64)
    LINK("env", "print_str", "v(ii)", m3_print_str)

    // WASI
    LINK("wasi_snapshot_preview1", "fd_write",  "i(iiii)", m3_wasi_fd_write)
    LINK("wasi_snapshot_preview1", "fd_seek",   "i(iIii)", m3_wasi_fd_seek)
    LINK("wasi_snapshot_preview1", "fd_close",  "i(i)",    m3_wasi_fd_close)
    LINK("wasi_snapshot_preview1", "proc_exit", "v(i)",    m3_wasi_proc_exit)

    // LUT
    LINK("env", "lut_load",  "i(i)",  m3_lut_load)
    LINK("env", "lut_get",   "i(i)",  m3_lut_get)
    LINK("env", "lut_size",  "i()",   m3_lut_size)
    LINK("env", "lut_set",   "v(ii)", m3_lut_set)
    LINK("env", "lut_save",  "i(i)",  m3_lut_save)
    LINK("env", "lut_check", "i(i)",  m3_lut_check)

    return m3Err_none;
}

#undef LINK
