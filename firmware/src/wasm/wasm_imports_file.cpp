#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "main.h"
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

// ---- Tunables ----
#define WASM_MAX_OPEN_FILES  4
#define WASM_MAX_OPEN_DIRS   4
#define WASM_MAX_PATH_LEN  128

// ---- State ----
static FILE *wasm_files[WASM_MAX_OPEN_FILES] = {};
static DIR  *wasm_dirs [WASM_MAX_OPEN_DIRS]  = {};
// Parallel to wasm_dirs: the fully-prefixed LittleFS path, needed to stat entries
static char  wasm_dir_path[WASM_MAX_OPEN_DIRS][WASM_MAX_PATH_LEN + 16];

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
        default: return NULL;
    }
}

// ---- Path validation ----
static bool wasm_path_ok(const char *path, int len)
{
    if (len <= 0 || len >= WASM_MAX_PATH_LEN) return false;
    if (path[0] != '/') return false;
    for (int i = 0; i < len - 1; i++)
        if (path[i] == '.' && path[i+1] == '.') return false;
    if (len == 11 && memcmp(path, "/config.ini", 11) == 0) return false;
    return true;
}

// Extract a null-terminated path from WASM memory and validate it.
static bool wasm_extract_path_z(IM3Runtime rt, uint32_t ptr, char *out)
{
    if (ptr == 0) return false;
    int len = wasm_mem_strlen(rt, ptr);
    if (len <= 0 || len >= WASM_MAX_PATH_LEN) return false;
    wasm_mem_read(rt, ptr, out, (size_t)len);
    out[len] = '\0';
    return wasm_path_ok(out, len);
}

// ---- Slot helpers ----
static int  alloc_file_slot(void) { for (int i=0;i<WASM_MAX_OPEN_FILES;i++) if (!wasm_files[i]) return i; return -1; }
static int  alloc_dir_slot (void) { for (int i=0;i<WASM_MAX_OPEN_DIRS; i++) if (!wasm_dirs [i]) return i; return -1; }
static bool file_handle_ok(int h) { return h >= 0 && h < WASM_MAX_OPEN_FILES && wasm_files[h]; }
static bool dir_handle_ok (int h) { return h >= 0 && h < WASM_MAX_OPEN_DIRS  && wasm_dirs [h]; }

static int map_whence(int w)
{
    switch (w) { case 0: return SEEK_SET; case 1: return SEEK_CUR; case 2: return SEEK_END; default: return -1; }
}

// Called from wasm_run() on program exit
void wasm_close_all_files(void)
{
    for (int i = 0; i < WASM_MAX_OPEN_FILES; i++)
        if (wasm_files[i]) { fclose(wasm_files[i]); wasm_files[i] = NULL; }
    for (int i = 0; i < WASM_MAX_OPEN_DIRS; i++)
        if (wasm_dirs[i])  { closedir(wasm_dirs[i]); wasm_dirs[i] = NULL; }
}

// ============================================================================
//                              Open-file API
// ============================================================================

// i32 file_open(i32 path_ptr, i32 mode) -> handle or -1
m3ApiRawFunction(m3_file_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, mode);

    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);

    const char *fmode = mode_str(mode);
    if (!fmode) m3ApiReturn(-1);

    int slot = alloc_file_slot();
    if (slot < 0) m3ApiReturn(-1);

    char fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(fpath, sizeof(fpath), path);
    wasm_files[slot] = fopen(fpath, fmode);
    if (!wasm_files[slot]) m3ApiReturn(-1);
    m3ApiReturn(slot);
}

// void file_close(i32 handle)
m3ApiRawFunction(m3_file_close)
{
    m3ApiGetArg(int32_t, handle);
    if (file_handle_ok(handle)) {
        fclose(wasm_files[handle]);
        wasm_files[handle] = NULL;
    }
    m3ApiSuccess();
}

// i32 file_read(handle, buf_ptr, max_len) -> bytes or -1
m3ApiRawFunction(m3_file_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, max_len);

    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    if (max_len <= 0 || !wasm_mem_check(runtime, (uint32_t)buf_ptr, (size_t)max_len))
        m3ApiReturn(-1);

    // Stage through DRAM — WASM memory may be PSRAM-backed
    int total = 0;
    uint32_t pos = (uint32_t)buf_ptr;
    int remaining = max_len;
    while (remaining > 0) {
        uint8_t tmp[256];
        int chunk = remaining > (int)sizeof(tmp) ? (int)sizeof(tmp) : remaining;
        int n = (int)fread(tmp, 1, chunk, wasm_files[handle]);
        if (n > 0) {
            wasm_mem_write(runtime, pos, tmp, n);
            pos += n;
            total += n;
        }
        if (n < chunk) break;  // EOF or short read
        remaining -= n;
    }
    m3ApiReturn(total);
}

// i32 file_write(handle, buf_ptr, len) -> bytes or -1
m3ApiRawFunction(m3_file_write)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, len);

    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    if (len <= 0 || !wasm_mem_check(runtime, (uint32_t)buf_ptr, (size_t)len))
        m3ApiReturn(-1);

    int total = 0;
    uint32_t pos = (uint32_t)buf_ptr;
    int remaining = len;
    while (remaining > 0) {
        uint8_t tmp[256];
        int chunk = remaining > (int)sizeof(tmp) ? (int)sizeof(tmp) : remaining;
        wasm_mem_read(runtime, pos, tmp, chunk);
        int n = (int)fwrite(tmp, 1, chunk, wasm_files[handle]);
        total += n;
        if (n < chunk) break;
        pos += n;
        remaining -= n;
    }
    m3ApiReturn(total);
}

// i32 file_size(handle) -> size or -1
m3ApiRawFunction(m3_file_size)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    m3ApiReturn((int32_t)fsize(wasm_files[handle]));
}

// i32 file_seek(handle, offset, whence) -> 0 or -1
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

// i32 file_tell(handle) -> pos or -1
m3ApiRawFunction(m3_file_tell)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    long p = ftell(wasm_files[handle]);
    m3ApiReturn(p < 0 ? -1 : (int32_t)p);
}

// i32 file_eof(handle) -> 1/0
m3ApiRawFunction(m3_file_eof)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(1);  // invalid handle -> treat as EOF
    m3ApiReturn(feof(wasm_files[handle]) ? 1 : 0);
}

// i32 file_truncate(handle, length) -> 0 or -1
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

// i32 file_flush(handle) -> 0 or -1
m3ApiRawFunction(m3_file_flush)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    m3ApiReturn(fflush(wasm_files[handle]) == 0 ? 0 : -1);
}

// Shared line reader — reads into local buf (null not included), strips trailing \r,
// returns line length. Returns 0 at clean EOF (no bytes read).
static int readln_into(FILE *f, char *buf, int cap)
{
    int n = 0;
    int first = fgetc(f);
    if (first == EOF) return 0;  // clean EOF at line boundary
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

// i32 file_readln(handle, buf_ptr, buf_len) -> bytes or -1 (0 at EOF)
m3ApiRawFunction(m3_file_readln)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, buf_ptr);
    m3ApiGetArg(int32_t, buf_len);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);
    if (buf_len < 1 || !wasm_mem_check(runtime, (uint32_t)buf_ptr, (size_t)buf_len))
        m3ApiReturn(-1);

    char line[256];
    int cap = (buf_len - 1 < (int)sizeof(line)) ? (buf_len - 1) : (int)sizeof(line);
    int n = readln_into(wasm_files[handle], line, cap);
    line[n] = '\0';
    wasm_mem_write(runtime, (uint32_t)buf_ptr, line, (size_t)n + 1);
    m3ApiReturn(n);
}

// i32 file_readln_str(handle) -> pool_ptr or 0
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
    if (!wasm_mem_check(runtime, dst, n + 1)) m3ApiReturn(0);
    wasm_mem_write(runtime, dst, buf, n + 1);
    m3ApiReturn((int32_t)dst);
}

// i32 file_writeln(handle, str_ptr) -> 0 or -1
m3ApiRawFunction(m3_file_writeln)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, str_ptr);
    if (!file_handle_ok(handle)) m3ApiReturn(-1);

    int len = wasm_mem_strlen(runtime, (uint32_t)str_ptr);
    if (len < 0) m3ApiReturn(-1);

    uint32_t pos = (uint32_t)str_ptr;
    int remaining = len;
    while (remaining > 0) {
        char tmp[256];
        int chunk = remaining > (int)sizeof(tmp) ? (int)sizeof(tmp) : remaining;
        wasm_mem_read(runtime, pos, tmp, chunk);
        if ((int)fwrite(tmp, 1, chunk, wasm_files[handle]) != chunk) m3ApiReturn(-1);
        pos += chunk;
        remaining -= chunk;
    }
    if (fwrite("\n", 1, 1, wasm_files[handle]) != 1) m3ApiReturn(-1);
    m3ApiReturn(0);
}

// ============================================================================
//                           Path-based API
// ============================================================================

// i32 file_stat(path_ptr, out_ptr) -> 0 or -1
// out_ptr points to file_stat_t { i32 size; i32 type; i32 mtime; } (12 bytes)
m3ApiRawFunction(m3_file_stat)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    m3ApiGetArg(int32_t, out_ptr);

    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);
    if (!wasm_mem_check(runtime, (uint32_t)out_ptr, 12)) m3ApiReturn(-1);

    char fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(fpath, sizeof(fpath), path);
    struct stat st;
    if (stat(fpath, &st) != 0) m3ApiReturn(-1);

    int32_t out[3];
    bool is_dir = S_ISDIR(st.st_mode);
    out[0] = is_dir ? 0 : (int32_t)st.st_size;
    out[1] = is_dir ? 1 : (S_ISREG(st.st_mode) ? 0 : -1);
    out[2] = (int32_t)st.st_mtime;
    wasm_mem_write(runtime, (uint32_t)out_ptr, out, sizeof(out));
    m3ApiReturn(0);
}

// i32 file_delete(path_ptr) -> 0 or -1
m3ApiRawFunction(m3_file_delete)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);
    char fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(fpath, sizeof(fpath), path);
    m3ApiReturn(unlink(fpath) == 0 ? 0 : -1);
}

// i32 file_rename(old_ptr, new_ptr) -> 0 or -1
m3ApiRawFunction(m3_file_rename)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, old_ptr);
    m3ApiGetArg(int32_t, new_ptr);
    char op[WASM_MAX_PATH_LEN], np[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)old_ptr, op)) m3ApiReturn(-1);
    if (!wasm_extract_path_z(runtime, (uint32_t)new_ptr, np)) m3ApiReturn(-1);
    char of[WASM_MAX_PATH_LEN + 16], nf[WASM_MAX_PATH_LEN + 16];
    lfs_path(of, sizeof(of), op);
    lfs_path(nf, sizeof(nf), np);
    m3ApiReturn(rename(of, nf) == 0 ? 0 : -1);
}

// i32 file_mkdir(path_ptr) -> 0 or -1
m3ApiRawFunction(m3_file_mkdir)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);
    char fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(fpath, sizeof(fpath), path);
    m3ApiReturn(mkdir(fpath, 0755) == 0 ? 0 : -1);
}

// i32 file_rmdir(path_ptr) -> 0 or -1
m3ApiRawFunction(m3_file_rmdir)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);
    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);
    char fpath[WASM_MAX_PATH_LEN + 16];
    lfs_path(fpath, sizeof(fpath), path);
    m3ApiReturn(rmdir(fpath) == 0 ? 0 : -1);
}

// ============================================================================
//                         Directory iteration
// ============================================================================

// i32 dir_open(path_ptr) -> handle or -1
m3ApiRawFunction(m3_dir_open)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, path_ptr);

    char path[WASM_MAX_PATH_LEN];
    if (!wasm_extract_path_z(runtime, (uint32_t)path_ptr, path)) m3ApiReturn(-1);

    int slot = alloc_dir_slot();
    if (slot < 0) m3ApiReturn(-1);

    lfs_path(wasm_dir_path[slot], sizeof(wasm_dir_path[slot]), path);
    wasm_dirs[slot] = opendir(wasm_dir_path[slot]);
    if (!wasm_dirs[slot]) m3ApiReturn(-1);
    m3ApiReturn(slot);
}

// i32 dir_read(handle, out_ptr) -> 1 (entry) | 0 (end) | -1 (error)
// out_ptr points to dir_entry_t { i32 type; char name[256]; } (260 bytes)
m3ApiRawFunction(m3_dir_read)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, handle);
    m3ApiGetArg(int32_t, out_ptr);
    if (!dir_handle_ok(handle)) m3ApiReturn(-1);
    if (!wasm_mem_check(runtime, (uint32_t)out_ptr, 260)) m3ApiReturn(-1);

    struct dirent *ent;
    while ((ent = readdir(wasm_dirs[handle])) != NULL) {
        // Filter '.' and '..'
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' ||
             (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        // stat to determine type (matches dir_list() in commands.cpp)
        char fullpath[WASM_MAX_PATH_LEN + 16 + 256];
        snprintf(fullpath, sizeof(fullpath), "%s/%s",
                 wasm_dir_path[handle], ent->d_name);
        struct stat st;
        int32_t type;
        if (stat(fullpath, &st) != 0) {
            type = -1;
        } else {
            type = S_ISDIR(st.st_mode) ? 1 : (S_ISREG(st.st_mode) ? 0 : -1);
        }

        wasm_mem_write(runtime, (uint32_t)out_ptr, &type, sizeof(type));
        int nlen = (int)strlen(ent->d_name);
        if (nlen > 255) nlen = 255;
        wasm_mem_write(runtime, (uint32_t)out_ptr + 4, ent->d_name, (size_t)nlen);
        uint8_t zero = 0;
        wasm_mem_write(runtime, (uint32_t)out_ptr + 4 + nlen, &zero, 1);
        m3ApiReturn(1);
    }
    m3ApiReturn(0);  // end of directory
}

// void dir_close(handle)
m3ApiRawFunction(m3_dir_close)
{
    m3ApiGetArg(int32_t, handle);
    if (dir_handle_ok(handle)) {
        closedir(wasm_dirs[handle]);
        wasm_dirs[handle] = NULL;
    }
    m3ApiSuccess();
}

// ============================================================================
//                              Link table
// ============================================================================

#define LINK(name, sig, fn) \
    result = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (result && result != m3Err_functionLookupFailed) return result;

M3Result link_file_imports(IM3Module module)
{
    M3Result result;

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

#endif // INCLUDE_WASM
