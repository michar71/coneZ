#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "sim_config.h"
#include "m3_env.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define WASM_MAX_OPEN_FILES  4
#define WASM_MAX_OPEN_DIRS   4
#define WASM_MAX_PATH_LEN  128

static FILE *wasm_files[WASM_MAX_OPEN_FILES] = {};
static DIR  *wasm_dirs [WASM_MAX_OPEN_DIRS]  = {};
// Stored alongside each dir handle: the fully-resolved sandbox path
static std::string wasm_dir_path[WASM_MAX_OPEN_DIRS];

void wasm_close_all_files()
{
    for (int i = 0; i < WASM_MAX_OPEN_FILES; i++) {
        if (wasm_files[i]) { fclose(wasm_files[i]); wasm_files[i] = nullptr; }
    }
    for (int i = 0; i < WASM_MAX_OPEN_DIRS; i++) {
        if (wasm_dirs[i]) { closedir(wasm_dirs[i]); wasm_dirs[i] = nullptr; }
    }
}

// ---- Mode table (matches conez_api.h FILE_MODE_*) ----
static const char *mode_str(int mode)
{
    switch (mode) {
        case 0: return "rb";
        case 1: return "wb";
        case 2: return "ab";
        case 3: return "rb+";
        case 4: return "wb+";
        case 5: return "ab+";
        default: return nullptr;
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

// Extract null-terminated path from WASM memory and validate
static bool get_path_z(IM3Runtime runtime, int32_t ptr, char *out, int out_size)
{
    if (ptr == 0) return false;
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)ptr >= mem_size) return false;
    int len = wasm_strlen(mem, mem_size, (uint32_t)ptr);
    if (len <= 0 || len >= out_size) return false;
    memcpy(out, mem + ptr, len);
    out[len] = '\0';
    return valid_path(out);
}

// Resolve sandbox path
static std::string sandbox(const char *path)
{
    return simConfig().sandbox_path + path;
}

// Ensure parent directories exist for write/append modes
static void ensure_parent_dirs(const std::string &full)
{
    std::string dir = full.substr(0, full.rfind('/'));
    if (dir.empty()) return;
    for (size_t i = 1; i < dir.size(); i++) {
        if (dir[i] == '/') {
            dir[i] = '\0';
            mkdir(dir.c_str(), 0755);
            dir[i] = '/';
        }
    }
    mkdir(dir.c_str(), 0755);
}

// ---- Slot helpers ----
static int  alloc_file_slot() { for (int i=0;i<WASM_MAX_OPEN_FILES;i++) if (!wasm_files[i]) return i; return -1; }
static int  alloc_dir_slot () { for (int i=0;i<WASM_MAX_OPEN_DIRS; i++) if (!wasm_dirs [i]) return i; return -1; }
static bool file_handle_ok(int h) { return h >= 0 && h < WASM_MAX_OPEN_FILES && wasm_files[h]; }
static bool dir_handle_ok (int h) { return h >= 0 && h < WASM_MAX_OPEN_DIRS  && wasm_dirs [h]; }

static int map_whence(int w)
{
    switch (w) { case 0: return SEEK_SET; case 1: return SEEK_CUR; case 2: return SEEK_END; default: return -1; }
}

// ============================================================================
//                              Open-file API
// ============================================================================

m3ApiRawFunction(m3_file_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, mode);

    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);

    const char *fmode = mode_str(mode);
    if (!fmode) m3ApiReturn(-1);

    int slot = alloc_file_slot();
    if (slot < 0) m3ApiReturn(-1);

    std::string full = sandbox(path);
    // Create parent directories for write/append modes (1,2,4,5)
    if (mode == 1 || mode == 2 || mode == 4 || mode == 5) {
        ensure_parent_dirs(full);
    }

    FILE *f = fopen(full.c_str(), fmode);
    if (!f) m3ApiReturn(-1);
    wasm_files[slot] = f;
    m3ApiReturn(slot);
}

m3ApiRawFunction(m3_file_close)
{
    m3ApiGetArg(int32_t, handle);
    if (file_handle_ok(handle)) {
        fclose(wasm_files[handle]);
        wasm_files[handle] = nullptr;
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_file_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, max_len);

    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || max_len <= 0 || (uint32_t)buf_ptr + max_len > mem_size) m3ApiReturn(-1);

    int rd = (int)fread(mem + buf_ptr, 1, max_len, wasm_files[handle]);
    m3ApiReturn(rd);
}

m3ApiRawFunction(m3_file_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, len);

    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || len <= 0 || (uint32_t)buf_ptr + len > mem_size) m3ApiReturn(-1);

    int wr = (int)fwrite(mem + buf_ptr, 1, len, wasm_files[handle]);
    fflush(wasm_files[handle]);
    m3ApiReturn(wr);
}

m3ApiRawFunction(m3_file_size)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    long cur = ftell(wasm_files[handle]);
    fseek(wasm_files[handle], 0, SEEK_END);
    long sz = ftell(wasm_files[handle]);
    fseek(wasm_files[handle], cur, SEEK_SET);
    m3ApiReturn((int32_t)sz);
}

m3ApiRawFunction(m3_file_seek)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, offset);
    m3ApiGetArg(int32_t, whence);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    int w = map_whence(whence);
    if (w < 0) m3ApiReturn(-1);
    m3ApiReturn(fseek(wasm_files[handle], offset, w) == 0 ? 0 : -1);
}

m3ApiRawFunction(m3_file_tell)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    long p = ftell(wasm_files[handle]);
    m3ApiReturn(p < 0 ? -1 : (int32_t)p);
}

m3ApiRawFunction(m3_file_eof)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(1);
    m3ApiReturn(feof(wasm_files[handle]) ? 1 : 0);
}

m3ApiRawFunction(m3_file_truncate)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, length);
    if (!file_handle_ok(handle) || length < 0) m3ApiReturn(-1);
    fflush(wasm_files[handle]);
    int fd = fileno(wasm_files[handle]);
    if (fd < 0) m3ApiReturn(-1);
    m3ApiReturn(ftruncate(fd, length) == 0 ? 0 : -1);
}

m3ApiRawFunction(m3_file_flush)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    m3ApiReturn(fflush(wasm_files[handle]) == 0 ? 0 : -1);
}

// Shared line reader
static int readln_into(FILE *f, char *buf, int cap)
{
    int n = 0;
    int first = fgetc(f);
    if (first == EOF) return 0;
    if (first != '\n') {
        buf[n++] = (char)first;
        while (n < cap) {
            int c = fgetc(f);
            if (c == EOF || c == '\n') break;
            buf[n++] = (char)c;
        }
    }
    if (n > 0 && buf[n-1] == '\r') n--;
    return n;
}

m3ApiRawFunction(m3_file_readln)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, buf_len);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || buf_len < 1 || (uint32_t)buf_ptr + buf_len > mem_size) m3ApiReturn(-1);

    char line[256];
    int cap = (buf_len - 1 < (int)sizeof(line)) ? (buf_len - 1) : (int)sizeof(line);
    int n = readln_into(wasm_files[handle], line, cap);
    memcpy(mem + buf_ptr, line, n);
    mem[buf_ptr + n] = '\0';
    m3ApiReturn(n);
}

m3ApiRawFunction(m3_file_readln_str)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(0);

    char buf[256];
    int n = readln_into(wasm_files[handle], buf, (int)sizeof(buf) - 1);
    if (n == 0 && feof(wasm_files[handle])) m3ApiReturn(0);
    buf[n] = '\0';

    uint32_t dst = pool_alloc(runtime, n + 1);
    if (dst == 0) m3ApiReturn(0);
    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || dst + n + 1 > mem_size) m3ApiReturn(0);
    memcpy(mem + dst, buf, n + 1);
    m3ApiReturn((int32_t)dst);
}

m3ApiRawFunction(m3_file_writeln)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, str_ptr);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)str_ptr >= mem_size) m3ApiReturn(-1);

    int len = wasm_strlen(mem, mem_size, (uint32_t)str_ptr);
    if (len > 0) {
        if ((int)fwrite(mem + str_ptr, 1, len, wasm_files[handle]) != len) m3ApiReturn(-1);
    }
    if (fwrite("\n", 1, 1, wasm_files[handle]) != 1) m3ApiReturn(-1);
    fflush(wasm_files[handle]);
    m3ApiReturn(0);
}

// ============================================================================
//                           Path-based API
// ============================================================================

// file_stat_t: { i32 size; i32 type; i32 mtime; } — 12 bytes
m3ApiRawFunction(m3_file_stat)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, out_ptr);

    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)out_ptr + 12 > mem_size) m3ApiReturn(-1);

    struct stat st;
    if (stat(sandbox(path).c_str(), &st) != 0) m3ApiReturn(-1);

    int32_t out[3];
    bool is_dir = S_ISDIR(st.st_mode);
    out[0] = is_dir ? 0 : (int32_t)st.st_size;
    out[1] = is_dir ? 1 : (S_ISREG(st.st_mode) ? 0 : -1);
    out[2] = (int32_t)st.st_mtime;
    memcpy(mem + out_ptr, out, sizeof(out));
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_file_delete)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);
    m3ApiReturn(remove(sandbox(path).c_str()) == 0 ? 0 : -1);
}

m3ApiRawFunction(m3_file_rename)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, old_ptr);
    m3ApiGetArg(int32_t, new_ptr);
    char op[WASM_MAX_PATH_LEN], np[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, old_ptr, op, sizeof(op))) m3ApiReturn(-1);
    if (!get_path_z(runtime, new_ptr, np, sizeof(np))) m3ApiReturn(-1);
    m3ApiReturn(rename(sandbox(op).c_str(), sandbox(np).c_str()) == 0 ? 0 : -1);
}

m3ApiRawFunction(m3_file_mkdir)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);
    m3ApiReturn(mkdir(sandbox(path).c_str(), 0755) == 0 ? 0 : -1);
}

m3ApiRawFunction(m3_file_rmdir)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);
    m3ApiReturn(rmdir(sandbox(path).c_str()) == 0 ? 0 : -1);
}

// ============================================================================
//                         Directory iteration
// ============================================================================

m3ApiRawFunction(m3_dir_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);

    char path[WASM_MAX_PATH_LEN];
    if (!get_path_z(runtime, path_ptr, path, sizeof(path))) m3ApiReturn(-1);

    int slot = alloc_dir_slot();
    if (slot < 0) m3ApiReturn(-1);

    wasm_dir_path[slot] = sandbox(path);
    wasm_dirs[slot] = opendir(wasm_dir_path[slot].c_str());
    if (!wasm_dirs[slot]) m3ApiReturn(-1);
    m3ApiReturn(slot);
}

// dir_entry_t: { i32 type; char name[256]; } — 260 bytes
m3ApiRawFunction(m3_dir_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, out_ptr);
    if (!dir_handle_ok(handle)) m3ApiReturn(-1);

    uint32_t mem_size = 0;
    uint8_t *mem = m3_GetMemory(runtime, &mem_size, 0);
    if (!mem || (uint32_t)out_ptr + 260 > mem_size) m3ApiReturn(-1);

    struct dirent *ent;
    while ((ent = readdir(wasm_dirs[handle])) != nullptr) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' ||
             (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        std::string full = wasm_dir_path[handle] + "/" + ent->d_name;
        struct stat st;
        int32_t type;
        if (stat(full.c_str(), &st) != 0) {
            type = -1;
        } else {
            type = S_ISDIR(st.st_mode) ? 1 : (S_ISREG(st.st_mode) ? 0 : -1);
        }

        memcpy(mem + out_ptr, &type, sizeof(type));
        int nlen = (int)strlen(ent->d_name);
        if (nlen > 255) nlen = 255;
        memcpy(mem + out_ptr + 4, ent->d_name, nlen);
        mem[out_ptr + 4 + nlen] = '\0';
        m3ApiReturn(1);
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_dir_close)
{
    m3ApiGetArg(int32_t, handle);
    if (dir_handle_ok(handle)) {
        closedir(wasm_dirs[handle]);
        wasm_dirs[handle] = nullptr;
        wasm_dir_path[handle].clear();
    }
    m3ApiSuccess();
}

// ============================================================================
//                              Link
// ============================================================================

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_file_imports(IM3Module module)
{
    M3Result r;

    LINK("file_open",       "i(ii)",   m3_file_open)
    LINK("file_close",      "v(i)",    m3_file_close)
    LINK("file_read",       "i(iii)",  m3_file_read)
    LINK("file_write",      "i(iii)",  m3_file_write)
    LINK("file_size",       "i(i)",    m3_file_size)
    LINK("file_seek",       "i(iii)",  m3_file_seek)
    LINK("file_tell",       "i(i)",    m3_file_tell)
    LINK("file_eof",        "i(i)",    m3_file_eof)
    LINK("file_truncate",   "i(ii)",   m3_file_truncate)
    LINK("file_flush",      "i(i)",    m3_file_flush)
    LINK("file_readln",     "i(iii)",  m3_file_readln)
    LINK("file_readln_str", "i(i)",    m3_file_readln_str)
    LINK("file_writeln",    "i(ii)",   m3_file_writeln)

    LINK("file_stat",       "i(ii)",   m3_file_stat)
    LINK("file_delete",     "i(i)",    m3_file_delete)
    LINK("file_rename",     "i(ii)",   m3_file_rename)
    LINK("file_mkdir",      "i(i)",    m3_file_mkdir)
    LINK("file_rmdir",      "i(i)",    m3_file_rmdir)

    LINK("dir_open",        "i(i)",    m3_dir_open)
    LINK("dir_read",        "i(ii)",   m3_dir_read)
    LINK("dir_close",       "v(i)",    m3_dir_close)

    return m3Err_none;
}

#undef LINK
