#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "FS.h"
#include <LittleFS.h>

// ---------- File I/O state ----------

#define WASM_MAX_OPEN_FILES  4
#define WASM_MAX_PATH_LEN  128

static File wasm_files[WASM_MAX_OPEN_FILES];
static bool wasm_file_open[WASM_MAX_OPEN_FILES] = {false};

// Validate a WASM-supplied path: must start with '/', no '..', not /config.ini
static bool wasm_path_ok(const char *path, int len)
{
    if (len <= 0 || len >= WASM_MAX_PATH_LEN) return false;
    if (path[0] != '/') return false;
    // Block path traversal
    for (int i = 0; i < len - 1; i++) {
        if (path[i] == '.' && path[i+1] == '.') return false;
    }
    // Protect config file
    if (len == 11 && memcmp(path, "/config.ini", 11) == 0) return false;
    return true;
}

// Helper: extract validated path from WASM memory
static bool wasm_extract_path(IM3Runtime runtime, int32_t ptr, int32_t len, char *out) {
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || len <= 0 || len >= WASM_MAX_PATH_LEN || (uint32_t)ptr + len > mem_size)
        return false;
    memcpy(out, mem_base + ptr, len);
    out[len] = '\0';
    return wasm_path_ok(out, len);
}

// Close all open WASM file handles (called on cleanup)
void wasm_close_all_files(void)
{
    for (int i = 0; i < WASM_MAX_OPEN_FILES; i++) {
        if (wasm_file_open[i]) {
            wasm_files[i].close();
            wasm_file_open[i] = false;
        }
    }
}

// --- File I/O ---

// i32 file_open(i32 path_ptr, i32 path_len, i32 mode) -> handle or -1
// mode: 0=read, 1=write, 2=append
m3ApiRawFunction(m3_file_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);
    m3ApiGetArg(int32_t, mode);

    // Validate memory access
    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || (uint32_t)path_ptr + path_len > mem_size || path_len <= 0) {
        m3ApiReturn(-1);
    }

    // Copy path to null-terminated buffer
    char path[WASM_MAX_PATH_LEN];
    if (path_len >= WASM_MAX_PATH_LEN) m3ApiReturn(-1);
    memcpy(path, mem_base + path_ptr, path_len);
    path[path_len] = '\0';

    // Validate path
    if (!wasm_path_ok(path, path_len)) m3ApiReturn(-1);

    // Find a free slot
    int slot = -1;
    for (int i = 0; i < WASM_MAX_OPEN_FILES; i++) {
        if (!wasm_file_open[i]) { slot = i; break; }
    }
    if (slot < 0) m3ApiReturn(-1);

    // Open
    const char *fmode;
    switch (mode) {
        case 0:  fmode = FILE_READ;   break;
        case 1:  fmode = FILE_WRITE;  break;
        case 2:  fmode = FILE_APPEND; break;
        default: m3ApiReturn(-1);
    }

    wasm_files[slot] = LittleFS.open(path, fmode);
    if (!wasm_files[slot]) m3ApiReturn(-1);

    wasm_file_open[slot] = true;
    m3ApiReturn(slot);
}

// void file_close(i32 handle)
m3ApiRawFunction(m3_file_close)
{
    m3ApiGetArg(int32_t, handle);
    if (handle >= 0 && handle < WASM_MAX_OPEN_FILES && wasm_file_open[handle]) {
        wasm_files[handle].close();
        wasm_file_open[handle] = false;
    }
    m3ApiSuccess();
}

// i32 file_read(i32 handle, i32 buf_ptr, i32 max_len) -> bytes read or -1
m3ApiRawFunction(m3_file_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, max_len);

    if (handle < 0 || handle >= WASM_MAX_OPEN_FILES || !wasm_file_open[handle]) {
        m3ApiReturn(-1);
    }

    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || max_len <= 0 || (uint32_t)buf_ptr + max_len > mem_size) {
        m3ApiReturn(-1);
    }

    int bytes = wasm_files[handle].read(mem_base + buf_ptr, max_len);
    m3ApiReturn(bytes);
}

// i32 file_write(i32 handle, i32 buf_ptr, i32 len) -> bytes written or -1
m3ApiRawFunction(m3_file_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, len);

    if (handle < 0 || handle >= WASM_MAX_OPEN_FILES || !wasm_file_open[handle]) {
        m3ApiReturn(-1);
    }

    uint32_t mem_size = m3_GetMemorySize(runtime);
    uint8_t *mem_base = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem_base || len <= 0 || (uint32_t)buf_ptr + len > mem_size) {
        m3ApiReturn(-1);
    }

    int bytes = wasm_files[handle].write(mem_base + buf_ptr, len);
    m3ApiReturn(bytes);
}

// i32 file_size(i32 handle) -> file size or -1
m3ApiRawFunction(m3_file_size)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= WASM_MAX_OPEN_FILES || !wasm_file_open[handle]) {
        m3ApiReturn(-1);
    }

    m3ApiReturn((int32_t)wasm_files[handle].size());
}

// i32 file_seek(i32 handle, i32 pos) -> 1 on success, 0 on failure
m3ApiRawFunction(m3_file_seek)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, pos);

    if (handle < 0 || handle >= WASM_MAX_OPEN_FILES || !wasm_file_open[handle]) {
        m3ApiReturn(0);
    }

    m3ApiReturn(wasm_files[handle].seek(pos) ? 1 : 0);
}

// i32 file_tell(i32 handle) -> current position or -1
m3ApiRawFunction(m3_file_tell)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= WASM_MAX_OPEN_FILES || !wasm_file_open[handle]) {
        m3ApiReturn(-1);
    }

    m3ApiReturn((int32_t)wasm_files[handle].position());
}

// i32 file_exists(i32 path_ptr, i32 path_len) -> 1/0
m3ApiRawFunction(m3_file_exists) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);
    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path(runtime, path_ptr, path_len, path)) m3ApiReturn(0);
    m3ApiReturn(LittleFS.exists(path) ? 1 : 0);
}

// i32 file_delete(i32 path_ptr, i32 path_len) -> 1 on success, 0 on failure
m3ApiRawFunction(m3_file_delete) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);
    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path(runtime, path_ptr, path_len, path)) m3ApiReturn(0);
    m3ApiReturn(LittleFS.remove(path) ? 1 : 0);
}

// i32 file_rename(i32 old_ptr, i32 old_len, i32 new_ptr, i32 new_len) -> 1/0
m3ApiRawFunction(m3_file_rename) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, old_ptr);
    m3ApiGetArg(int32_t, old_len);
    m3ApiGetArg(int32_t, new_ptr);
    m3ApiGetArg(int32_t, new_len);
    char old_path[WASM_MAX_PATH_LEN], new_path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path(runtime, old_ptr, old_len, old_path)) m3ApiReturn(0);
    if (!wasm_extract_path(runtime, new_ptr, new_len, new_path)) m3ApiReturn(0);
    m3ApiReturn(LittleFS.rename(old_path, new_path) ? 1 : 0);
}


// ---------- Link file imports ----------

M3Result link_file_imports(IM3Module module)
{
    M3Result result;

    result = m3_LinkRawFunction(module, "env", "file_open", "i(iii)", m3_file_open);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_close", "v(i)", m3_file_close);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_read", "i(iii)", m3_file_read);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_write", "i(iii)", m3_file_write);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_size", "i(i)", m3_file_size);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_seek", "i(ii)", m3_file_seek);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_tell", "i(i)", m3_file_tell);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "file_exists", "i(ii)", m3_file_exists);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "file_delete", "i(ii)", m3_file_delete);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "file_rename", "i(iiii)", m3_file_rename);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
