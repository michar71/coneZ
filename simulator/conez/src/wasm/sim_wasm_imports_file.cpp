#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "sim_config.h"
#include "m3_env.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_OPEN_FILES 4

static FILE *open_files[MAX_OPEN_FILES] = {};

void wasm_close_all_files()
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (open_files[i]) {
            fclose(open_files[i]);
            open_files[i] = nullptr;
        }
    }
}

// Validate path: must start with /, no .., not /config.ini
static bool valid_path(const char *p)
{
    if (!p || p[0] != '/') return false;
    if (strstr(p, "..")) return false;
    if (strcmp(p, "/config.ini") == 0) return false;
    return true;
}

// Resolve sandbox path
static std::string sandbox(const char *path)
{
    return simConfig().sandbox_path + path;
}

// Extract path string from WASM memory
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

m3ApiRawFunction(m3_file_open) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);
    m3ApiGetArg(int32_t, mode);

    char path[256];
    if (!get_path(runtime, path_ptr, path_len, path, sizeof(path))) { m3ApiReturn(-1); }
    if (!valid_path(path)) { m3ApiReturn(-1); }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i]) { slot = i; break; }
    }
    if (slot < 0) { m3ApiReturn(-1); }

    const char *fmode;
    switch (mode) {
        case 0: fmode = "rb"; break;
        case 1: fmode = "wb"; break;
        case 2: fmode = "ab"; break;
        default: m3ApiReturn(-1);
    }

    std::string full = sandbox(path);

    // Create parent directories for write/append
    if (mode == 1 || mode == 2) {
        std::string dir = full.substr(0, full.rfind('/'));
        if (!dir.empty()) {
            // mkdir -p equivalent
            for (size_t i = 1; i < dir.size(); i++) {
                if (dir[i] == '/') {
                    dir[i] = '\0';
                    mkdir(dir.c_str(), 0755);
                    dir[i] = '/';
                }
            }
            mkdir(dir.c_str(), 0755);
        }
    }

    FILE *f = fopen(full.c_str(), fmode);
    if (!f) { m3ApiReturn(-1); }

    open_files[slot] = f;
    m3ApiReturn(slot);
}

m3ApiRawFunction(m3_file_close) {
    m3ApiGetArg(int32_t, handle);
    if (handle >= 0 && handle < MAX_OPEN_FILES && open_files[handle]) {
        fclose(open_files[handle]);
        open_files[handle] = nullptr;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_file_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, max_len);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(-1); }

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)buf_ptr + max_len > mem_size) { m3ApiReturn(-1); }

    int rd = (int)fread(mem + buf_ptr, 1, max_len, open_files[handle]);
    m3ApiReturn(rd);
}

m3ApiRawFunction(m3_file_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, len);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(-1); }

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)buf_ptr + len > mem_size) { m3ApiReturn(-1); }

    int wr = (int)fwrite(mem + buf_ptr, 1, len, open_files[handle]);
    fflush(open_files[handle]);
    m3ApiReturn(wr);
}

m3ApiRawFunction(m3_file_size) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(-1); }

    long cur = ftell(open_files[handle]);
    fseek(open_files[handle], 0, SEEK_END);
    long sz = ftell(open_files[handle]);
    fseek(open_files[handle], cur, SEEK_SET);
    m3ApiReturn((int32_t)sz);
}

m3ApiRawFunction(m3_file_seek) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, pos);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(0); }
    m3ApiReturn(fseek(open_files[handle], pos, SEEK_SET) == 0 ? 1 : 0);
}

m3ApiRawFunction(m3_file_tell) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(-1); }
    m3ApiReturn((int32_t)ftell(open_files[handle]));
}

m3ApiRawFunction(m3_file_exists) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);

    char path[256];
    if (!get_path(runtime, path_ptr, path_len, path, sizeof(path))) { m3ApiReturn(0); }
    if (!valid_path(path)) { m3ApiReturn(0); }

    m3ApiReturn(access(sandbox(path).c_str(), F_OK) == 0 ? 1 : 0);
}

m3ApiRawFunction(m3_file_delete) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);

    char path[256];
    if (!get_path(runtime, path_ptr, path_len, path, sizeof(path))) { m3ApiReturn(0); }
    if (!valid_path(path)) { m3ApiReturn(0); }

    m3ApiReturn(remove(sandbox(path).c_str()) == 0 ? 1 : 0);
}

m3ApiRawFunction(m3_file_rename) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, old_ptr);
    m3ApiGetArg(int32_t, old_len);
    m3ApiGetArg(int32_t, new_ptr);
    m3ApiGetArg(int32_t, new_len);

    char old_path[256], new_path[256];
    if (!get_path(runtime, old_ptr, old_len, old_path, sizeof(old_path))) { m3ApiReturn(0); }
    if (!get_path(runtime, new_ptr, new_len, new_path, sizeof(new_path))) { m3ApiReturn(0); }
    if (!valid_path(old_path) || !valid_path(new_path)) { m3ApiReturn(0); }

    m3ApiReturn(rename(sandbox(old_path).c_str(), sandbox(new_path).c_str()) == 0 ? 1 : 0);
}

m3ApiRawFunction(m3_file_mkdir) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);

    char path[256];
    if (!get_path(runtime, path_ptr, path_len, path, sizeof(path))) { m3ApiReturn(0); }
    if (!valid_path(path)) { m3ApiReturn(0); }

    m3ApiReturn(mkdir(sandbox(path).c_str(), 0755) == 0 ? 1 : 0);
}

m3ApiRawFunction(m3_file_rmdir) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, path_len);

    char path[256];
    if (!get_path(runtime, path_ptr, path_len, path, sizeof(path))) { m3ApiReturn(0); }
    if (!valid_path(path)) { m3ApiReturn(0); }

    m3ApiReturn(rmdir(sandbox(path).c_str()) == 0 ? 1 : 0);
}

// ---- BASIC-friendly file I/O (uses string pool) ----

m3ApiRawFunction(m3_basic_file_open) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, str_ptr);  // pool pointer to null-terminated path
    m3ApiGetArg(int32_t, mode);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || str_ptr >= (int32_t)mem_size) { m3ApiReturn(-1); }

    int len = wasm_strlen(mem, mem_size, str_ptr);
    if (len <= 0) { m3ApiReturn(-1); }

    char path[256];
    if (len >= (int)sizeof(path)) { m3ApiReturn(-1); }
    memcpy(path, mem + str_ptr, len);
    path[len] = '\0';
    if (!valid_path(path)) { m3ApiReturn(-1); }

    int slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i]) { slot = i; break; }
    }
    if (slot < 0) { m3ApiReturn(-1); }

    const char *fmode;
    switch (mode) {
        case 0: fmode = "rb"; break;
        case 1: fmode = "wb"; break;
        case 2: fmode = "ab"; break;
        default: m3ApiReturn(-1);
    }

    std::string full = sandbox(path);
    if (mode == 1 || mode == 2) {
        std::string dir = full.substr(0, full.rfind('/'));
        if (!dir.empty()) {
            for (size_t i = 1; i < dir.size(); i++) {
                if (dir[i] == '/') { dir[i] = '\0'; mkdir(dir.c_str(), 0755); dir[i] = '/'; }
            }
            mkdir(dir.c_str(), 0755);
        }
    }

    FILE *f = fopen(full.c_str(), fmode);
    if (!f) { m3ApiReturn(-1); }
    open_files[slot] = f;
    m3ApiReturn(slot);
}

m3ApiRawFunction(m3_basic_file_close) {
    m3ApiGetArg(int32_t, handle);
    if (handle >= 0 && handle < MAX_OPEN_FILES && open_files[handle]) {
        fclose(open_files[handle]);
        open_files[handle] = nullptr;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_basic_file_print) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, str_ptr);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(0); }

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || str_ptr >= (int32_t)mem_size) { m3ApiReturn(0); }

    int len = wasm_strlen(mem, mem_size, str_ptr);
    if (len <= 0) { m3ApiReturn(1); } // empty string is OK

    int wr = (int)fwrite(mem + str_ptr, 1, len, open_files[handle]);
    fwrite("\n", 1, 1, open_files[handle]);
    fflush(open_files[handle]);
    m3ApiReturn(wr > 0 ? 1 : 0);
}

m3ApiRawFunction(m3_basic_file_readln) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(0); }

    char line[1024];
    if (!fgets(line, sizeof(line), open_files[handle])) { m3ApiReturn(0); }

    // Strip trailing newline
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    line[len] = '\0';

    // Allocate in string pool
    uint32_t ptr = pool_alloc(runtime, len + 1);
    if (!ptr) { m3ApiReturn(0); }

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (mem && ptr + len + 1 <= mem_size) {
        memcpy(mem + ptr, line, len + 1);
    }
    m3ApiReturn((int32_t)ptr);
}

m3ApiRawFunction(m3_basic_file_eof) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);

    if (handle < 0 || handle >= MAX_OPEN_FILES || !open_files[handle]) { m3ApiReturn(1); }
    m3ApiReturn(feof(open_files[handle]) ? 1 : 0);
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_file_imports(IM3Module module)
{
    M3Result r;

    LINK("file_open",   "i(iii)",  m3_file_open)
    LINK("file_close",  "v(i)",    m3_file_close)
    LINK("file_read",   "i(iii)",  m3_file_read)
    LINK("file_write",  "i(iii)",  m3_file_write)
    LINK("file_size",   "i(i)",    m3_file_size)
    LINK("file_seek",   "i(ii)",   m3_file_seek)
    LINK("file_tell",   "i(i)",    m3_file_tell)
    LINK("file_exists", "i(ii)",   m3_file_exists)
    LINK("file_delete", "i(ii)",   m3_file_delete)
    LINK("file_rename", "i(iiii)", m3_file_rename)
    LINK("file_mkdir",  "i(ii)",   m3_file_mkdir)
    LINK("file_rmdir",  "i(ii)",   m3_file_rmdir)

    // BASIC-friendly
    LINK("basic_file_open",   "i(ii)",  m3_basic_file_open)
    LINK("basic_file_close",  "v(i)",   m3_basic_file_close)
    LINK("basic_file_print",  "i(ii)",  m3_basic_file_print)
    LINK("basic_file_readln", "i(i)",   m3_basic_file_readln)
    LINK("basic_file_eof",    "i(i)",   m3_basic_file_eof)

    return m3Err_none;
}

#undef LINK
