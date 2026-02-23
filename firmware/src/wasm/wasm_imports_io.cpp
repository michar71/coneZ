#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "printManager.h"
#include "lut.h"
#include <string.h>

// --- Output ---

// void print_i32(i32 val)
m3ApiRawFunction(m3_print_i32)
{
    m3ApiGetArg(int32_t, val);
    printfnl(SOURCE_WASM, "%d\n", val);
    m3ApiSuccess();
}

// void print_f32(f32 val)
m3ApiRawFunction(m3_print_f32)
{
    m3ApiGetArg(float, val);
    printfnl(SOURCE_WASM, "%f\n", val);
    m3ApiSuccess();
}

// void print_i64(I64 val)
m3ApiRawFunction(m3_print_i64)
{
    m3ApiGetArg(int64_t, val);
    printfnl(SOURCE_WASM, "%lld\n", (long long)val);
    m3ApiSuccess();
}

// void print_f64(F64 val)
m3ApiRawFunction(m3_print_f64)
{
    m3ApiGetArg(double, val);
    printfnl(SOURCE_WASM, "%g\n", val);
    m3ApiSuccess();
}

// void print_str(i32 ptr, i32 len) — reads string from WASM linear memory
m3ApiRawFunction(m3_print_str)
{
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(int32_t, len);

    if (len < 0 || !wasm_mem_check(runtime, (uint32_t)offset, (size_t)len)) {
        m3ApiTrap("print_str: out of bounds");
    }

    // Print up to 255 chars at a time to avoid huge stack buffers
    uint32_t pos = (uint32_t)offset;
    int remaining = len;
    while (remaining > 0) {
        int chunk = remaining > 255 ? 255 : remaining;
        char buf[256];
        wasm_mem_read(runtime, pos, buf, chunk);
        buf[chunk] = '\0';
        printfnl(SOURCE_WASM, "%s", buf);
        pos += chunk;
        remaining -= chunk;
    }

    m3ApiSuccess();
}

// --- WASI stubs ---

// i32 fd_write(i32 fd, i32 iovs_ptr, i32 iovs_count, i32 nwritten_ptr)
// Minimal WASI fd_write: stdout (fd=1) and stderr (fd=2) only
m3ApiRawFunction(m3_wasi_fd_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fd);
    m3ApiGetArg(int32_t, iovs_ptr);
    m3ApiGetArg(int32_t, iovs_count);
    m3ApiGetArg(int32_t, nwritten_ptr);

    // Only stdout and stderr
    if (fd != 1 && fd != 2) {
        m3ApiReturn(8);  // WASI EBADF
    }

    // Validate iov array bounds
    if (!wasm_mem_check(runtime, (uint32_t)iovs_ptr, (uint32_t)iovs_count * 8) ||
        !wasm_mem_check(runtime, (uint32_t)nwritten_ptr, 4)) {
        m3ApiReturn(28);  // WASI EINVAL
    }

    int32_t total = 0;
    for (int32_t i = 0; i < iovs_count; i++) {
        uint32_t iov_off = (uint32_t)iovs_ptr + i * 8;
        uint32_t buf_ptr, buf_len;
        wasm_mem_read(runtime, iov_off, &buf_ptr, 4);
        wasm_mem_read(runtime, iov_off + 4, &buf_len, 4);

        if (buf_len == 0) continue;
        if (!wasm_mem_check(runtime, buf_ptr, buf_len)) {
            m3ApiReturn(28);  // WASI EINVAL
        }

        // Print in chunks
        uint32_t pos = buf_ptr;
        int remaining = (int)buf_len;
        while (remaining > 0) {
            int chunk = remaining > 255 ? 255 : remaining;
            char buf[256];
            wasm_mem_read(runtime, pos, buf, chunk);
            buf[chunk] = '\0';
            printfnl(SOURCE_WASM, "%s", buf);
            pos += chunk;
            remaining -= chunk;
        }
        total += (int32_t)buf_len;
    }

    // Write total bytes to nwritten_ptr
    wasm_mem_write(runtime, (uint32_t)nwritten_ptr, &total, 4);

    m3ApiReturn(0);  // success
}

// i32 fd_seek(i32 fd, i64 offset, i32 whence, i32 newoffset_ptr) -> errno
m3ApiRawFunction(m3_wasi_fd_seek)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fd);
    m3ApiGetArg(int64_t, offset);
    m3ApiGetArg(int32_t, whence);
    m3ApiGetArg(int32_t, newoffset_ptr);
    (void)fd; (void)offset; (void)whence; (void)newoffset_ptr;
    m3ApiReturn(8);  // WASI EBADF
}

// i32 fd_close(i32 fd) -> errno
m3ApiRawFunction(m3_wasi_fd_close)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, fd);
    (void)fd;
    m3ApiReturn(8);  // WASI EBADF
}

// void proc_exit(i32 code)
m3ApiRawFunction(m3_wasi_proc_exit)
{
    m3ApiGetArg(int32_t, code);
    (void)code;
    wasm_stop_requested = true;
    m3ApiTrap(m3Err_trapExit);
}

// --- LUT ---

// i32 lut_load(i32 index) -> entry count or 0 on failure
m3ApiRawFunction(m3_lut_load)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(0);
    m3ApiReturn(loadLut((uint8_t)index));
}

// i32 lut_get(i32 index) -> value or 0 if out of bounds
m3ApiRawFunction(m3_lut_get)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (!pLUT || index < 0 || index >= lutSize) m3ApiReturn(0);
    m3ApiReturn(pLUT[index]);
}

// i32 lut_size() -> current LUT size
m3ApiRawFunction(m3_lut_size)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(lutSize);
}

// void lut_set(i32 index, i32 value) — bounds-checked
m3ApiRawFunction(m3_lut_set)
{
    m3ApiGetArg(int32_t, index);
    m3ApiGetArg(int32_t, value);
    if (pLUT && index >= 0 && index < lutSize) {
        pLUT[index] = value;
    }
    m3ApiSuccess();
}

// i32 lut_save(i32 index) -> 1 on success, 0 on failure
m3ApiRawFunction(m3_lut_save)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(0);
    m3ApiReturn(saveLut((uint8_t)index));
}

// i32 lut_check(i32 index) -> entry count or -1 if not found
m3ApiRawFunction(m3_lut_check)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, index);
    if (index < 0 || index > 255) m3ApiReturn(-1);
    m3ApiReturn(checkLut((uint8_t)index));
}


// ---------- Link I/O imports ----------

M3Result link_io_imports(IM3Module module)
{
    M3Result result;

    // Output
    result = m3_LinkRawFunction(module, "env", "print_i32", "v(i)", m3_print_i32);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_f32", "v(f)", m3_print_f32);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_i64", "v(I)", m3_print_i64);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_f64", "v(F)", m3_print_f64);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "print_str", "v(ii)", m3_print_str);
    if (result && result != m3Err_functionLookupFailed) return result;

    // WASI
    result = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_write", "i(iiii)", m3_wasi_fd_write);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_seek", "i(iIii)", m3_wasi_fd_seek);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "fd_close", "i(i)", m3_wasi_fd_close);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "wasi_snapshot_preview1", "proc_exit", "v(i)", m3_wasi_proc_exit);
    if (result && result != m3Err_functionLookupFailed) return result;

    // LUT
    result = m3_LinkRawFunction(module, "env", "lut_load", "i(i)", m3_lut_load);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_get", "i(i)", m3_lut_get);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_size", "i()", m3_lut_size);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_set", "v(ii)", m3_lut_set);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_save", "i(i)", m3_lut_save);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "lut_check", "i(i)", m3_lut_check);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
