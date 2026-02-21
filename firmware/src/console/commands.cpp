#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include "shell.h"
#include "conez_wifi.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include "basic_wrapper.h"
#ifdef INCLUDE_WASM
#include "wasm_wrapper.h"
#endif
#include "main.h"
#include "freertos/task.h"
#include "printManager.h"
#include "gps.h"
#include "sensors.h"
#include "lora.h"
#include "led.h"
#include "config.h"
#include "cue.h"
#include "sun.h"
#include "editor.h"
#include "psram.h"
#include "conez_mqtt.h"
#include "inflate.h"
#include "deflate.h"
#include "glob.h"
#include "mbedtls/md5.h"
#include "mbedtls/sha256.h"
#include "lwip/stats.h"
#include <csetjmp>
/*
 * Forward-declare only the embedded compiler APIs we need.
 * Can't include both bas2wasm.h and c2wasm.h in the same TU because
 * they share type/macro names (Buf, buf_init, source, etc.).
 */
extern "C" {

#ifdef INCLUDE_BASIC_COMPILER
typedef void (*bw_diag_fn)(const char *msg, void *ctx);
extern bw_diag_fn bw_on_error;
extern bw_diag_fn bw_on_info;
extern void *bw_cb_ctx;
extern jmp_buf bw_bail;

typedef struct { uint8_t *data; int len, cap; } bw_Buf;
bw_Buf bas2wasm_compile_buffer(const char *src, int len);
void bas2wasm_reset(void);
void bw_buf_init(bw_Buf *b);
void bw_buf_free(bw_Buf *b);
const char *bas2wasm_version_string(void);
#endif

#ifdef INCLUDE_C_COMPILER
typedef void (*cw_diag_fn)(const char *msg, void *ctx);
extern cw_diag_fn cw_on_error;
extern cw_diag_fn cw_on_info;
extern void *cw_cb_ctx;
extern jmp_buf cw_bail;

typedef struct { uint8_t *data; int len, cap; } cw_Buf;
cw_Buf c2wasm_compile_buffer(const char *src, int len, const char *filename);
void c2wasm_reset(void);
void cw_buf_init(cw_Buf *b);
void cw_buf_free(cw_Buf *b);
const char *c2wasm_version_string(void);
#endif

} /* extern "C" */


// Parse an integer from a string, supporting decimal and 0x hex prefix.
static inline int parse_int(const char *s) { return (int)strtol(s, NULL, 0); }

//Serial/Telnet Shell comamnds

void renameFile(fs::FS &fs, const char *path1, const char *path2) 
{
    printfnl(SOURCE_COMMANDS, "Renaming file %s to %s\r\n", path1, path2);
    if (fs.rename(path1, path2)) {
      printfnl(SOURCE_COMMANDS, "- file renamed\n" );
    } else {
      printfnl(SOURCE_COMMANDS, "- rename failed\n" );
    }
}
  
void deleteFile(fs::FS &fs, const char *path)
{
    printfnl(SOURCE_COMMANDS, "Deleting file: %s\r\n", path);
    if (fs.remove(path)) {
      printfnl(SOURCE_COMMANDS, "- file deleted\n" );
    } else {
      printfnl(SOURCE_COMMANDS, "- delete failed\n" );
    }
}


// Directory entry for sorted, aligned listing
struct DirEntry {
    char name[64];
    uint32_t size;
    time_t mtime;
    bool isDir;
};

static int dir_entry_cmp(const void *a, const void *b)
{
    const DirEntry *ea = (const DirEntry *)a;
    const DirEntry *eb = (const DirEntry *)b;
    // Directories first, then alphabetical
    if (ea->isDir != eb->isDir) return ea->isDir ? -1 : 1;
    return strcasecmp(ea->name, eb->name);
}

static int effective_tz_offset(int year, int month, int day);
static const char *tz_label(int tz_hours);

static void dir_list(fs::FS &fs, const char *dirname, int indent,
                     Stream *out, bool showTime, int nameWidth,
                     int *fileCount, int *dirCount, uint32_t *totalSize,
                     const char *filter = NULL)
{
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return;

    // Collect entries (heap-allocated to avoid stack overflow on recursion)
    const int MAX_ENTRIES = 32;
    DirEntry *entries = (DirEntry *)malloc(MAX_ENTRIES * sizeof(DirEntry));
    if (!entries) { root.close(); return; }

    int n = 0;
    File file = root.openNextFile();
    while (file && n < MAX_ENTRIES) {
        // Apply filter to files only (directories always shown)
        if (filter && !file.isDirectory() && !glob_match(filter, file.name())) {
            file = root.openNextFile();
            continue;
        }
        DirEntry *e = &entries[n];
        strncpy(e->name, file.name(), sizeof(e->name) - 1);
        e->name[sizeof(e->name) - 1] = '\0';
        e->isDir = file.isDirectory();
        e->size = e->isDir ? 0 : file.size();
        e->mtime = file.getLastWrite();
        n++;
        file = root.openNextFile();
    }
    root.close();

    qsort(entries, n, sizeof(DirEntry), dir_entry_cmp);

    // Print entries — dirs first (with trailing /), then files
    for (int i = 0; i < n; i++) {
        DirEntry *e = &entries[i];
        if (e->isDir) {
            out->printf("%*s%s/\n", indent, "", e->name);
            (*dirCount)++;
            // Recurse with increased indent (filter only applies to top level)
            char subpath[128];
            snprintf(subpath, sizeof(subpath), "%s%s%s",
                     dirname, (dirname[strlen(dirname)-1] == '/') ? "" : "/",
                     e->name);
            dir_list(fs, subpath, indent + 2, out, showTime, nameWidth,
                     fileCount, dirCount, totalSize);
        } else {
            *totalSize += e->size;
            (*fileCount)++;
            if (showTime && e->mtime > 0) {
                struct tm tm;
                int utc_y = get_year(), utc_m = get_month(), utc_d = get_day();
                time_t local_t = e->mtime + effective_tz_offset(utc_y, utc_m, utc_d) * 3600;
                gmtime_r(&local_t, &tm);
                out->printf("%*s%-*s  %6u  %s %02d %02d:%02d\n",
                    indent, "", nameWidth, e->name, e->size,
                    (const char *[]){"Jan","Feb","Mar","Apr","May","Jun",
                     "Jul","Aug","Sep","Oct","Nov","Dec"}[tm.tm_mon],
                    tm.tm_mday, tm.tm_hour, tm.tm_min);
            } else {
                out->printf("%*s%-*s  %6u\n",
                    indent, "", nameWidth, e->name, e->size);
            }
        }
    }
    free(entries);
}

// Pre-scan to find longest filename at any level
static void dir_max_name(fs::FS &fs, const char *dirname, int *maxLen)
{
    File root = fs.open(dirname);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        int len = strlen(file.name());
        if (len > *maxLen) *maxLen = len;
        if (file.isDirectory()) {
            char subpath[128];
            snprintf(subpath, sizeof(subpath), "%s%s%s",
                     dirname, (dirname[strlen(dirname)-1] == '/') ? "" : "/",
                     file.name());
            dir_max_name(fs, subpath, maxLen);
        }
        file = root.openNextFile();
    }
}

void readFile(fs::FS &fs, const char *path) 
{
    printfnl(SOURCE_COMMANDS,"Listing file: %s\r\n\n", path);
  
    File file = fs.open(path);
    if (!file || file.isDirectory()) 
    {
      printfnl(SOURCE_COMMANDS, "- failed to open file for reading\n" );
      return;
    }
  
    char buf[128];
    while (file.available())
    {
      int len = file.readBytesUntil('\n', buf, sizeof(buf) - 1);
      buf[len] = '\0';
      printfnl(SOURCE_COMMANDS, "%s\n", buf);
    }
    printfnl(SOURCE_COMMANDS, "\n" );
    printfnl(SOURCE_COMMANDS, "- file read complete\n" );
    file.close();
  }
  
  void writeFile(fs::FS &fs, const char *path, const char *message) 
  {
    printfnl(SOURCE_COMMANDS, "Writing file: %s\r\n", path);
  
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      printfnl(SOURCE_COMMANDS, "- failed to open file for writing\n" );
      return;
    }
    if (file.print(message)) 
    {
      printfnl(SOURCE_COMMANDS, "- file written\n" );
    } 
    else 
    {
      printfnl(SOURCE_COMMANDS, "- write failed\n" );
    }
    file.close();
  }

/*
Commands
*/
int test(int argc, char **argv) 
{
  printfnl(SOURCE_COMMANDS, "Test function called with %d Arguments\n", argc);
  printfnl(SOURCE_COMMANDS, " Arguments:\n" );
  for (int ii=0;ii<argc;ii++)
  {
    printfnl(SOURCE_COMMANDS, "Argument %d: %s\n", ii, argv[ii]);
  }  
  return 0;
};


int cmd_reboot( int argc, char **argv )
{
    printfnl( SOURCE_SYSTEM, "Rebooting...\n" );
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return 0;
}


int cmd_debug( int argc, char **argv )
{
    // If no args, show current debug message config.
    if( argc < 2 )
    {
        printfnl(SOURCE_COMMANDS, "Current Debug Settings:\n" );

        printfnl(SOURCE_COMMANDS, " - SYSTEM: \t%s\n", getDebug(SOURCE_SYSTEM) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - BASIC: \t%s\n", getDebug(SOURCE_BASIC) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - WASM: \t%s\n", getDebug(SOURCE_WASM) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - COMMANDS: \t%s\n", getDebug(SOURCE_COMMANDS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - SHELL: \t%s\n", getDebug(SOURCE_SHELL) ? "on" : "off" );        
        printfnl(SOURCE_COMMANDS, " - GPS: \t%s\n", getDebug(SOURCE_GPS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - GPS_RAW: \t%s\n", getDebug(SOURCE_GPS_RAW) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - LORA: \t%s\n", getDebug(SOURCE_LORA) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - LORA_RAW: \t%s\n", getDebug(SOURCE_LORA_RAW) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - FSYNC: \t%s\n", getDebug(SOURCE_FSYNC) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - WIFI: \t%s\n", getDebug(SOURCE_WIFI) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - SENSORS: \t%s\n", getDebug(SOURCE_SENSORS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - MQTT: \t%s\n", getDebug(SOURCE_MQTT) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, " - OTHER: \t%s\n", getDebug(SOURCE_OTHER) ? "on" : "off" );

        return 0;
    }

    // Turn off all debug messages?
    if( !strcasecmp( argv[1], "off" ) )
    {
        setDebugOff();
        return 0;
    }

    uint32_t mask_to_set = 0;
    if( !strcasecmp( argv[1], "SYSTEM" ) )
        mask_to_set = SOURCE_SYSTEM;
    else
    if( !strcasecmp( argv[1], "BASIC" ) )
        mask_to_set = SOURCE_BASIC;
    else
    if( !strcasecmp( argv[1], "WASM" ) )
        mask_to_set = SOURCE_WASM;
    else
    if( !strcasecmp( argv[1], "COMMANDS" ) )
        mask_to_set = SOURCE_COMMANDS;
    else
    if( !strcasecmp( argv[1], "SHELL" ) )
        mask_to_set = SOURCE_SHELL;
    else
    if( !strcasecmp( argv[1], "GPS" ) )
        mask_to_set = SOURCE_GPS;
    else
    if( !strcasecmp( argv[1], "GPS_RAW" ) )
        mask_to_set = SOURCE_GPS_RAW;
    else
    if( !strcasecmp( argv[1], "LORA" ) )
        mask_to_set = SOURCE_LORA;
    else
    if( !strcasecmp( argv[1], "LORA_RAW" ) )
        mask_to_set = SOURCE_LORA_RAW;
    else
    if( !strcasecmp( argv[1], "WIFI" ) )
        mask_to_set = SOURCE_WIFI;
    else
    if( !strcasecmp( argv[1], "FSYNC" ) )
        mask_to_set = SOURCE_FSYNC;
    else
    if( !strcasecmp( argv[1], "OTHER" ) )
        mask_to_set = SOURCE_OTHER;
    else       
    if( !strcasecmp( argv[1], "SENSORS" ) )
        mask_to_set = SOURCE_SENSORS;
    else
    if( !strcasecmp( argv[1], "MQTT" ) )
        mask_to_set = SOURCE_MQTT;
    else            
    {
        printfnl(SOURCE_COMMANDS, "Debug name \"%s\"not recognized.\n", argv[1] );
        return 1;
    }

    // If someone just does "debug {name}", treat the same as "debug {name} on"
    if( argc == 2 )
        setDebugLevel( (source_e) mask_to_set, true );
    else
    if( argc >= 3 )
    {
        if( !strcasecmp( argv[2], "off" ) )
            setDebugLevel((source_e)mask_to_set, false);
        else
            setDebugLevel((source_e)mask_to_set, true);
    }
    
    return 0;
}

int delFile(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: del <file ...>\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[64];
        normalize_path(path, sizeof(path), argv[i]);
        if (has_glob_chars(path)) {
            char (*matches)[128];
            int count = glob_expand(path, &matches);
            if (!count) {
                printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[i]);
                rc = 1;
                continue;
            }
            for (int j = 0; j < count; j++)
                deleteFile(LittleFS, matches[j]);
            free(matches);
        } else {
            deleteFile(LittleFS, path);
        }
    }
    return rc;
}

// Build destination path: if dst is a directory, append basename of src
static void resolve_dest(char *dst, size_t dstsz, const char *src)
{
    File d = LittleFS.open(dst);
    if (d && d.isDirectory()) {
        d.close();
        const char *name = strrchr(src, '/');
        name = name ? name + 1 : src;
        char tmp[64];
        size_t len = strlen(dst);
        if (len > 0 && dst[len - 1] == '/')
            snprintf(tmp, sizeof(tmp), "%s%s", dst, name);
        else
            snprintf(tmp, sizeof(tmp), "%s/%s", dst, name);
        strlcpy(dst, tmp, dstsz);
    } else if (d) {
        d.close();
    }
}

int renFile(int argc, char **argv)
{
    if (argc != 3) {
        printfnl(SOURCE_COMMANDS, "Usage: mv <source> <dest>\n");
        return 1;
    }
    char path1[64], path2[64];
    normalize_path(path1, sizeof(path1), argv[1]);
    normalize_path(path2, sizeof(path2), argv[2]);

    if (has_glob_chars(path2)) {
        printfnl(SOURCE_COMMANDS, "Wildcards not allowed in destination\n");
        return 1;
    }

    if (has_glob_chars(path1)) {
        char (*matches)[128];
        int count = glob_expand(path1, &matches);
        if (!count) {
            printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[1]);
            return 1;
        }
        // Dest must be a directory for wildcard move
        File d = LittleFS.open(path2);
        bool isDir = d && d.isDirectory();
        if (d) d.close();
        if (!isDir) {
            printfnl(SOURCE_COMMANDS, "Destination must be a directory for wildcard move\n");
            free(matches);
            return 1;
        }
        int rc = 0;
        for (int j = 0; j < count; j++) {
            char destpath[64];
            strlcpy(destpath, path2, sizeof(destpath));
            resolve_dest(destpath, sizeof(destpath), matches[j]);
            if (strcmp(matches[j], destpath) != 0)
                renameFile(LittleFS, matches[j], destpath);
        }
        free(matches);
        return rc;
    }

    // Non-glob path
    resolve_dest(path2, sizeof(path2), path1);

    if (strcmp(path1, path2) == 0) {
        printfnl(SOURCE_COMMANDS, "Source and destination are the same file\n");
        return 1;
    }

    renameFile(LittleFS, path1, path2);
    return 0;
}

int listFile(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: cat <file ...>\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[64];
        normalize_path(path, sizeof(path), argv[i]);
        if (has_glob_chars(path)) {
            char (*matches)[128];
            int count = glob_expand(path, &matches);
            if (!count) {
                printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[i]);
                rc = 1;
                continue;
            }
            for (int j = 0; j < count; j++) {
                readFile(LittleFS, matches[j]);
                printfnl(SOURCE_COMMANDS, "\n");
            }
            free(matches);
        } else {
            readFile(LittleFS, path);
            printfnl(SOURCE_COMMANDS, "\n");
        }
    }
    return rc;
}

int listDir(int argc, char **argv)
{
    char path[64];
    const char *filter = NULL;

    if (argc >= 2) {
        normalize_path(path, sizeof(path), argv[1]);
        // If arg has wildcards, split into dir + filter pattern
        if (has_glob_chars(path)) {
            const char *lastSlash = strrchr(path, '/');
            if (lastSlash) {
                filter = lastSlash + 1;
                // Trim path to directory part
                if (lastSlash == path)
                    path[1] = '\0';  // root "/"
                else
                    path[lastSlash - path] = '\0';
            }
        }
    } else {
        strcpy(path, "/");
    }

    File root = LittleFS.open(path);
    if (!root || !root.isDirectory()) {
        printfnl(SOURCE_COMMANDS, "Not a directory: %s\n", path);
        return 1;
    }
    root.close();

    int nameWidth = 20;
    bool showTime = get_time_valid();
    int fileCount = 0, dirCount = 0;
    uint32_t totalSize = 0;

    getLock();
    Stream *out = getStream();
    dir_list(LittleFS, path, 0, out, showTime, nameWidth,
             &fileCount, &dirCount, &totalSize, filter);
    out->printf("%d file%s, %d dir%s, %u bytes\n",
        fileCount, fileCount == 1 ? "" : "s",
        dirCount, dirCount == 1 ? "" : "s",
        totalSize);
    releaseLock();
    return 0;
}

int cmd_df(int argc, char **argv)
{
    size_t total = LittleFS.totalBytes();
    size_t used  = LittleFS.usedBytes();
    size_t free  = total - used;
    unsigned pct = total ? (unsigned)(used * 100 / total) : 0;

    printfnl(SOURCE_COMMANDS, "Filesystem: LittleFS\n");
    printfnl(SOURCE_COMMANDS, "  Total: %u bytes (%u KB)\n", (unsigned)total, (unsigned)(total / 1024));
    printfnl(SOURCE_COMMANDS, "  Used:  %u bytes (%u KB)  %u%%\n", (unsigned)used, (unsigned)(used / 1024), pct);
    printfnl(SOURCE_COMMANDS, "  Free:  %u bytes (%u KB)\n", (unsigned)free, (unsigned)(free / 1024));
    return 0;
}


static void grep_file(const char *pattern, const char *path, bool show_filename)
{
    File f = LittleFS.open(path, "r");
    if (!f || f.isDirectory()) return;

    char buf[256];
    int lineno = 0;
    while (f.available()) {
        int len = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
        buf[len] = '\0';
        if (len > 0 && buf[len - 1] == '\r') buf[--len] = '\0';
        lineno++;

        // Case-insensitive substring search
        // Build lowercase copies for comparison
        char lower_buf[256], lower_pat[64];
        for (int i = 0; buf[i]; i++)
            lower_buf[i] = (buf[i] >= 'A' && buf[i] <= 'Z') ? buf[i] + 32 : buf[i];
        lower_buf[len] = '\0';
        int plen = strlen(pattern);
        for (int i = 0; i < plen && i < 63; i++)
            lower_pat[i] = (pattern[i] >= 'A' && pattern[i] <= 'Z') ? pattern[i] + 32 : pattern[i];
        lower_pat[plen < 63 ? plen : 63] = '\0';

        if (strstr(lower_buf, lower_pat)) {
            if (show_filename)
                printfnl(SOURCE_NONE, "%s:%d: %s\n", path, lineno, buf);
            else
                printfnl(SOURCE_NONE, "%3d: %s\n", lineno, buf);
        }
    }
    f.close();
}

static void grep_dir(const char *pattern, const char *dirname)
{
    File root = LittleFS.open(dirname);
    if (!root || !root.isDirectory()) return;

    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            char subpath[128];
            snprintf(subpath, sizeof(subpath), "%s%s%s",
                     dirname, (dirname[strlen(dirname)-1] == '/') ? "" : "/",
                     file.name());
            grep_dir(pattern, subpath);
        } else {
            char filepath[128];
            snprintf(filepath, sizeof(filepath), "%s%s%s",
                     dirname, (dirname[strlen(dirname)-1] == '/') ? "" : "/",
                     file.name());
            grep_file(pattern, filepath, true);
        }
        file = root.openNextFile();
    }
}

int cmd_grep(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: grep <pattern> [file]  (no file = search all)\n");
        return 1;
    }

    if (argc >= 3) {
        char path[64];
        normalize_path(path, sizeof(path), argv[2]);
        if (has_glob_chars(path)) {
            char (*matches)[128];
            int count = glob_expand(path, &matches);
            if (!count) {
                printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[2]);
                return 1;
            }
            for (int j = 0; j < count; j++)
                grep_file(argv[1], matches[j], count > 1);
            free(matches);
        } else {
            grep_file(argv[1], path, false);
        }
    } else {
        // Search all files recursively
        grep_dir(argv[1], "/");
    }
    return 0;
}


static int copy_file(const char *src, const char *dst)
{
    File in = LittleFS.open(src, "r");
    if (!in) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", src);
        return 1;
    }
    File out = LittleFS.open(dst, FILE_WRITE);
    if (!out) {
        in.close();
        printfnl(SOURCE_COMMANDS, "Cannot create %s\n", dst);
        return 1;
    }

    uint8_t buf[256];
    size_t total = 0;
    while (in.available()) {
        size_t n = in.read(buf, sizeof(buf));
        out.write(buf, n);
        total += n;
    }
    in.close();
    out.close();
    printfnl(SOURCE_COMMANDS, "Copied %u bytes: %s -> %s\n", (unsigned)total, src, dst);
    return 0;
}

int cmd_cp(int argc, char **argv)
{
    if (argc != 3) {
        printfnl(SOURCE_COMMANDS, "Usage: cp <source> <dest>\n");
        return 1;
    }
    char src[64], dst[64];
    normalize_path(src, sizeof(src), argv[1]);
    normalize_path(dst, sizeof(dst), argv[2]);

    if (has_glob_chars(dst)) {
        printfnl(SOURCE_COMMANDS, "Wildcards not allowed in destination\n");
        return 1;
    }

    if (has_glob_chars(src)) {
        char (*matches)[128];
        int count = glob_expand(src, &matches);
        if (!count) {
            printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[1]);
            return 1;
        }
        // Dest must be a directory for wildcard copy
        File d = LittleFS.open(dst);
        bool isDir = d && d.isDirectory();
        if (d) d.close();
        if (!isDir) {
            printfnl(SOURCE_COMMANDS, "Destination must be a directory for wildcard copy\n");
            free(matches);
            return 1;
        }
        int rc = 0;
        for (int j = 0; j < count; j++) {
            char destpath[64];
            strlcpy(destpath, dst, sizeof(destpath));
            resolve_dest(destpath, sizeof(destpath), matches[j]);
            if (strcmp(matches[j], destpath) != 0)
                if (copy_file(matches[j], destpath)) rc = 1;
        }
        free(matches);
        return rc;
    }

    // Non-glob path
    resolve_dest(dst, sizeof(dst), src);

    if (strcmp(src, dst) == 0) {
        printfnl(SOURCE_COMMANDS, "Source and destination are the same file\n");
        return 1;
    }

    return copy_file(src, dst);
}

// Streaming write callback for LittleFS File
static int file_write_cb(const uint8_t *data, size_t len, void *ctx)
{
    File *f = (File *)ctx;
    return f->write(data, len) == len ? 0 : -1;
}

// Decompress a gzip, zlib, or raw deflate file using streaming inflate
int cmd_inflate(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        printfnl(SOURCE_COMMANDS, "Usage: inflate <input> [output]\n");
        printfnl(SOURCE_COMMANDS, "  Decompresses gzip (.gz), zlib, or raw deflate files.\n");
        printfnl(SOURCE_COMMANDS, "  Output defaults to input with .gz stripped, or input.out\n");
        return 1;
    }

    char src[64], dst[64];
    normalize_path(src, sizeof(src), argv[1]);

    if (argc == 3) {
        normalize_path(dst, sizeof(dst), argv[2]);
    } else {
        // Strip .gz extension, or append .out
        strlcpy(dst, src, sizeof(dst));
        int len = strlen(dst);
        if (len > 3 && strcmp(dst + len - 3, ".gz") == 0)
            dst[len - 3] = '\0';
        else if (len > 2 && strcmp(dst + len - 2, ".z") == 0)
            dst[len - 2] = '\0';
        else
            strlcat(dst, ".out", sizeof(dst));
    }

    // Read entire compressed file into heap
    File in = LittleFS.open(src, "r");
    if (!in) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", src);
        return 1;
    }
    size_t in_size = in.size();
    if (in_size == 0) {
        in.close();
        printfnl(SOURCE_COMMANDS, "File is empty\n");
        return 1;
    }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) {
        in.close();
        printfnl(SOURCE_COMMANDS, "Out of memory (%u bytes)\n", (unsigned)in_size);
        return 1;
    }
    in.read(in_buf, in_size);
    in.close();

    // Stream decompressed chunks directly to output file
    File out = LittleFS.open(dst, FILE_WRITE);
    if (!out) {
        free(in_buf);
        printfnl(SOURCE_COMMANDS, "Cannot create %s\n", dst);
        return 1;
    }

    int result = inflate_stream(in_buf, in_size, file_write_cb, &out);
    out.close();
    free(in_buf);

    if (result < 0) {
        LittleFS.remove(dst);
        printfnl(SOURCE_COMMANDS, "Decompression error\n");
        return 1;
    }

    printfnl(SOURCE_COMMANDS, "Inflated: %s (%u -> %u bytes)\n",
        dst, (unsigned)in_size, (unsigned)result);
    return 0;
}

// Compress a file to gzip format using streaming deflate
int cmd_deflate(int argc, char **argv)
{
    if (argc < 2 || argc > 4) {
        printfnl(SOURCE_COMMANDS, "Usage: deflate <input> [output] [level]\n");
        printfnl(SOURCE_COMMANDS, "  Compresses a file to gzip format.\n");
        printfnl(SOURCE_COMMANDS, "  Output defaults to input.gz; level 0-10 (default 6)\n");
        return 1;
    }

    char src[64], dst[64];
    normalize_path(src, sizeof(src), argv[1]);

    if (argc >= 3 && argv[2][0] != '\0' && (argv[2][0] < '0' || argv[2][0] > '9')) {
        normalize_path(dst, sizeof(dst), argv[2]);
    } else {
        strlcpy(dst, src, sizeof(dst));
        strlcat(dst, ".gz", sizeof(dst));
    }

    int level = 6;
    if (argc == 4) level = parse_int(argv[3]);
    else if (argc == 3 && argv[2][0] >= '0' && argv[2][0] <= '9') level = parse_int(argv[2]);
    if (level < 0) level = 0;
    if (level > 10) level = 10;

    File in = LittleFS.open(src, "r");
    if (!in) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", src);
        return 1;
    }
    size_t in_size = in.size();
    if (in_size == 0) {
        in.close();
        printfnl(SOURCE_COMMANDS, "File is empty\n");
        return 1;
    }
    uint8_t *in_buf = (uint8_t *)malloc(in_size);
    if (!in_buf) {
        in.close();
        printfnl(SOURCE_COMMANDS, "Out of memory (%u bytes)\n", (unsigned)in_size);
        return 1;
    }
    in.read(in_buf, in_size);
    in.close();

    File out = LittleFS.open(dst, FILE_WRITE);
    if (!out) {
        free(in_buf);
        printfnl(SOURCE_COMMANDS, "Cannot create %s\n", dst);
        return 1;
    }

    int result = gzip_stream(in_buf, in_size, file_write_cb, &out, 15, 8, level);
    out.close();
    free(in_buf);

    if (result < 0) {
        LittleFS.remove(dst);
        printfnl(SOURCE_COMMANDS, "Compression error\n");
        return 1;
    }

    printfnl(SOURCE_COMMANDS, "Deflated: %s (%u -> %u bytes)\n",
        dst, (unsigned)in_size, (unsigned)result);
    return 0;
}


int cmd_hexdump(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: hexdump <filename> [count]\n");
        return 1;
    }
    char path[64];
    normalize_path(path, sizeof(path), argv[1]);

    File f = LittleFS.open(path, "r");
    if (!f) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", path);
        return 1;
    }

    int limit = (argc >= 3) ? parse_int(argv[2]) : 256;
    if (limit <= 0) limit = 256;

    size_t fsize = f.size();
    printfnl(SOURCE_COMMANDS, "%s  (%u bytes)\n", path, (unsigned)fsize);

    uint8_t buf[16];
    int offset = 0;
    while (f.available() && offset < limit) {
        int n = f.read(buf, 16);
        if (n <= 0) break;
        if (offset + n > limit) n = limit - offset;

        // Address
        getLock();
        Stream *out = getStream();
        out->printf("%04x  ", offset);

        // Hex bytes
        for (int i = 0; i < 16; i++) {
            if (i == 8) out->print(' ');
            if (i < n)
                out->printf("%02x ", buf[i]);
            else
                out->print("   ");
        }

        // ASCII
        out->print(" |");
        for (int i = 0; i < n; i++)
            out->write((buf[i] >= 32 && buf[i] < 127) ? buf[i] : '.');
        out->println("|");
        releaseLock();

        offset += n;
    }
    f.close();
    if ((int)fsize > limit)
        printfnl(SOURCE_COMMANDS, "... (%u more bytes)\n", (unsigned)(fsize - limit));
    return 0;
}

int cmd_mkdir(int argc, char **argv)
{
    if (argc != 2) {
        printfnl(SOURCE_COMMANDS, "Usage: mkdir <dirname>\n");
        return 1;
    }
    char path[64];
    normalize_path(path, sizeof(path), argv[1]);
    if (LittleFS.mkdir(path))
        printfnl(SOURCE_COMMANDS, "Created %s\n", path);
    else
        printfnl(SOURCE_COMMANDS, "Failed to create %s\n", path);
    return 0;
}

int cmd_rmdir(int argc, char **argv)
{
    if (argc != 2) {
        printfnl(SOURCE_COMMANDS, "Usage: rmdir <dirname>\n");
        return 1;
    }
    char path[64];
    normalize_path(path, sizeof(path), argv[1]);
    if (LittleFS.rmdir(path))
        printfnl(SOURCE_COMMANDS, "Removed %s\n", path);
    else
        printfnl(SOURCE_COMMANDS, "Failed to remove %s (not empty?)\n", path);
    return 0;
}

int loadFile(int argc, char **argv)
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, "Wrong argument count\n" );
        return 1;
    }
    else
    {
        char path[64];
        normalize_path(path, sizeof(path), argv[1]);
        int linecount = 0;
        int charcount = 0;
        char line[256];
        char inchar;
        bool isDone = false;
        printfnl(SOURCE_COMMANDS, "Ready for file. Press CTRL+Z to end transmission and save file %s\n", path);
        //Flush serial buffer
        getLock();
        getStream()->flush();
        //create file
        File file = LittleFS.open(path, FILE_WRITE);
        if (!file)
        {
            releaseLock();
            printfnl(SOURCE_COMMANDS, "- failed to open file for writing\n" );
            return 1;
        }

        do
        {
            //Get one character from serial port
            if (getStream()->available())
            {
                inchar = getStream()->read();
                //Check if its a break character
                if (inchar == 0x1A) 
                {
                    //Break loop 
                    break;
                }
                else
                {
                    //Wait for a full line
                    line[charcount] = inchar;
                    charcount++;
                    if (charcount>254)
                    {
                        getStream()->printf("Line %d too long\n",linecount+1);
                        break;
                    }
                    if (inchar == '\n')
                    {
                        //Write line
                        line[charcount] = '\0';
                        if (file.print(line))
                        {
                        } 
                        else 
                        {
                          getStream()->printf("Write Error\n");
                          file.close();
                          releaseLock();
                          return 1;
                        }
                        //increase line counter
                        linecount++;
                        //clear line
                        charcount = 0;
                        line[0] = 0;
            
                    }
                }
            }
        }
        while (isDone == false);
        //close file
        file.close();
        releaseLock();
        printfnl(SOURCE_COMMANDS, "%d Lines written to file\n", linecount);
        
        return 0;
    }

}

int runBasic(int argc, char **argv)
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, "Usage: run <file.bas|file.wasm>\n" );
        return 1;
    }
    else
    {
        char path[64];
        normalize_path(path, sizeof(path), argv[1]);
        if (false == set_script_program(path))
          printfnl(SOURCE_COMMANDS, "Unknown script type (use .bas or .wasm)\n" );
        return 0;
    }
}

int stopBasic(int argc, char **argv)
{
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, "Wrong argument count\n" );
        return 1;
    }
    else
    {
        set_basic_param(0,1);
#ifdef INCLUDE_WASM
        wasm_request_stop();
#endif
        return 0;
    }
}

int paramBasic(int argc, char **argv) 
{
    if (argc != 3)
    {
        printfnl(SOURCE_COMMANDS, "Wrong argument count\n" );
        return 1;       
    }
    else
    {
        set_basic_param(parse_int(argv[1]),parse_int(argv[2]));      
        return 0;
    }
}

int cmd_mem(int argc, char **argv)
{
    printfnl(SOURCE_COMMANDS, "Heap:\n" );
    printfnl(SOURCE_COMMANDS, "  Free:    %u bytes\n", esp_get_free_heap_size() );
    printfnl(SOURCE_COMMANDS, "  Min:     %u bytes  (lowest since boot)\n", esp_get_minimum_free_heap_size() );
    printfnl(SOURCE_COMMANDS, "  Largest: %u bytes  (biggest allocatable block)\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) );

    printfnl(SOURCE_COMMANDS, "\nPSRAM:\n" );
    if (psram_available()) {
        printfnl(SOURCE_COMMANDS, "  Size:       %u bytes (%u KB)\n", psram_size(), psram_size()/1024);
        printfnl(SOURCE_COMMANDS, "  Used:       %u bytes\n", psram_bytes_used());
        printfnl(SOURCE_COMMANDS, "  Free:       %u bytes\n", psram_bytes_free());
        printfnl(SOURCE_COMMANDS, "  Contiguous: %u bytes\n", psram_bytes_contiguous());
        printfnl(SOURCE_COMMANDS, "  Alloc slots: %d / %d\n", psram_alloc_count(), psram_alloc_entries_max());
    } else {
        printfnl(SOURCE_COMMANDS, "  Not available (using heap fallback)\n" );
    }

    return 0;
}


int cmd_ps(int argc, char **argv)
{
    UBaseType_t numTasks = uxTaskGetNumberOfTasks();
    TaskStatus_t *taskList = (TaskStatus_t *)malloc(numTasks * sizeof(TaskStatus_t));
    if (!taskList) {
        printfnl(SOURCE_COMMANDS, "Out of memory\n");
        return -1;
    }

    UBaseType_t got = uxTaskGetSystemState(taskList, numTasks, NULL);

    printfnl(SOURCE_COMMANDS, "Task List (%u tasks):\n", (unsigned int)got);
    printfnl(SOURCE_COMMANDS, "  %-16s %-6s %4s  %4s  %s\n", "Name", "State", "Prio", "Core", "Min Free Stack");

    for (UBaseType_t i = 0; i < got; i++)
    {
        const char *state;
        switch (taskList[i].eCurrentState)
        {
            case eRunning:   state = "Run";   break;
            case eReady:     state = "Ready"; break;
            case eBlocked:   state = "Block"; break;
            case eSuspended: state = "Susp";  break;
            case eDeleted:   state = "Del";   break;
            default:         state = "?";     break;
        }

        BaseType_t coreId = xTaskGetAffinity(taskList[i].xHandle);
        uint32_t freeStackBytes = (uint32_t)taskList[i].usStackHighWaterMark * 4;

        if (coreId == tskNO_AFFINITY)
            printfnl(SOURCE_COMMANDS, "  %-16s %-6s %4u     -  %u\n",
                taskList[i].pcTaskName, state,
                (unsigned int)taskList[i].uxCurrentPriority,
                (unsigned int)freeStackBytes);
        else
            printfnl(SOURCE_COMMANDS, "  %-16s %-6s %4u  %4d  %u\n",
                taskList[i].pcTaskName, state,
                (unsigned int)taskList[i].uxCurrentPriority,
                (int)coreId,
                (unsigned int)freeStackBytes);
    }

    free(taskList);
    return 0;
}


int tc(int argc, char **argv)
{
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, "Wrong argument count\n" );
        return 1;
    }
    else
    {
        printfnl(SOURCE_COMMANDS,"Thread Count:\n" );
        for (int ii=0;ii<4;ii++)
        {
            printfnl( SOURCE_COMMANDS, "Core %d: %d\n", (uint8_t)ii, (unsigned int)get_thread_count(ii) );
        }
        return 0;
    }
}


int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    const char *ver = "?";
    const char *proj = "conez";
    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK) {
        ver = desc.version;
        proj = desc.project_name;
    }

    unsigned long ms = uptime_ms();
    unsigned long totalSec = ms / 1000;
    unsigned int days  = totalSec / 86400;
    unsigned int hours = (totalSec % 86400) / 3600;
    unsigned int mins  = (totalSec % 3600) / 60;

    const char *board_name =
#ifdef BOARD_CONEZ_V0_1
        "conez-v0-1";
#elif defined(BOARD_HELTEC_LORA32_V3)
        "heltec-lora32-v3";
#else
        "unknown";
#endif

    getLock();
    Stream *out = getStream();

    // Version + uptime
    out->printf("ConeZ %s  %s  up %ud %02uh %02um\n", ver, board_name, days, hours, mins);

    // Cone identity
    out->printf("Cone:    id=%d  group=%d\n", config.cone_id, config.cone_group);

    // WiFi
    if (!config.wifi_enabled) {
        out->printf("WiFi:    Disabled\n");
    } else if (wifi_is_connected()) {
        char ip[16];
        wifi_get_ip_str(ip, sizeof(ip));
        out->printf("WiFi:    Connected  %s  RSSI %d dBm\n", ip, (int)wifi_get_rssi());
    } else {
        out->printf("WiFi:    Disconnected  (SSID: %s)\n", config.wifi_ssid);
    }

    // MQTT
    if (!config.mqtt_enabled) {
        out->printf("MQTT:    Disabled\n");
    } else if (mqtt_connected()) {
        out->printf("MQTT:    Connected  %s:%d  TX:%lu RX:%lu\n",
            config.mqtt_broker, config.mqtt_port,
            (unsigned long)mqtt_tx_count(), (unsigned long)mqtt_rx_count());
    } else {
        out->printf("MQTT:    %s  %s:%d\n",
            mqtt_state_str(), config.mqtt_broker, config.mqtt_port);
    }

    // LoRa
    out->printf("LoRa:    %s  %.1f MHz  TX:%lu RX:%lu\n",
        lora_get_mode(), lora_get_frequency(),
        (unsigned long)lora_get_tx_count(), (unsigned long)lora_get_rx_count());

    // GPS
#ifdef BOARD_HAS_GPS
    {
        static const char *fix_names[] = { "Unknown", "No Fix", "2D", "3D" };
        int ft = get_fix_type();
        const char *fix_str = (ft >= 0 && ft <= 3) ? fix_names[ft] : "Unknown";
        if (get_gpsstatus())
            out->printf("GPS:     %s  %d sats  %.6f %.6f\n",
                fix_str, get_satellites(), get_lat(), get_lon());
        else
            out->printf("GPS:     %s  %d sats\n", fix_str, get_satellites());
    }
#endif

    // Time
    if (get_time_valid()) {
        uint64_t epoch = get_epoch_ms();
        int utc_y = get_year(), utc_m = get_month(), utc_d = get_day();
        int tz = effective_tz_offset(utc_y, utc_m, utc_d);
        time_t local_t = (time_t)(epoch / 1000) + tz * 3600;
        struct tm ltm;
        gmtime_r(&local_t, &ltm);

        uint8_t ts = get_time_source();
        const char *src = "none";
        if (ts == 2)      src = "GPS+PPS";
        else if (ts == 1) src = "NTP";
        else if (get_time_valid()) src = "build";

        out->printf("Time:    %04d-%02d-%02d %02d:%02d:%02d %s  source=%s  NTP=%s\n",
            ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
            ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
            tz_label(tz), src, config.ntp_server);
    } else {
        out->printf("Time:    not available  NTP=%s\n", config.ntp_server);
    }

    // Script
#ifdef INCLUDE_WASM
    if (wasm_is_running()) {
        const char *p = wasm_get_current_path();
        out->printf("Script:  %s (running)\n", p ? p : "?");
    } else {
        out->printf("Script:  idle\n");
    }
#endif

    // Cue
    if (cue_is_playing())
        out->printf("Cue:     playing  elapsed %lu ms\n", (unsigned long)cue_get_elapsed_ms());
    else
        out->printf("Cue:     idle\n");

    // Heap
    out->printf("Heap:    %u free  (min %u)\n",
        (unsigned)esp_get_free_heap_size(), (unsigned)esp_get_minimum_free_heap_size());

    // PSRAM
    if (psram_available())
        out->printf("PSRAM:   %u KB  used %u  free %u\n",
            (unsigned)(psram_size() / 1024),
            (unsigned)(psram_bytes_used() / 1024),
            (unsigned)(psram_bytes_free() / 1024));
    else
        out->printf("PSRAM:   not available\n");

    // LEDs
#ifdef BOARD_HAS_RGB_LEDS
    out->printf("LEDs:    ch1=%d ch2=%d ch3=%d ch4=%d\n",
        config.led_count1, config.led_count2, config.led_count3, config.led_count4);
#endif

    // Sensors
    out->printf("Sensors: IMU=%s  temp=%.1fC  bat=%.2fV  solar=%.2fV\n",
        imuAvailable() ? "yes" : "no", getTemp(), bat_voltage(), solar_voltage());

    releaseLock();
    return 0;
}


int cmd_version(int argc, char **argv)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t desc;

    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK)
    {
        printfnl(SOURCE_COMMANDS, "Firmware: %s\n", desc.project_name);
        printfnl(SOURCE_COMMANDS, "Version: %s\n", desc.version);
        printfnl(SOURCE_COMMANDS, "Built:   %s %s\n", desc.date, desc.time);
    }
    else
    {
        printfnl(SOURCE_COMMANDS, "Firmware info unavailable\n");
    }

#ifdef BOARD_CONEZ_V0_1
    printfnl(SOURCE_COMMANDS, "Board:   conez-v0-1\n");
#elif defined(BOARD_HELTEC_LORA32_V3)
    printfnl(SOURCE_COMMANDS, "Board:   heltec-lora32-v3\n");
#else
    printfnl(SOURCE_COMMANDS, "Board:   unknown\n");
#endif

#ifdef INCLUDE_BASIC_COMPILER
    printfnl(SOURCE_COMMANDS, "%s\n", bas2wasm_version_string());
#endif
#ifdef INCLUDE_C_COMPILER
    printfnl(SOURCE_COMMANDS, "%s\n", c2wasm_version_string());
#endif

    // List all app partitions with firmware versions
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP,
                                                     ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (it) {
        printfnl(SOURCE_COMMANDS, "\nPartitions:\n");
        while (it != NULL) {
            const esp_partition_t* part = esp_partition_get(it);
            bool hasInfo = esp_ota_get_partition_description(part, &desc) == ESP_OK;

            const char *tag = "";
            if (part == running) tag = " [RUNNING]";
            else if (part == boot) tag = " [BOOT]";

            printfnl(SOURCE_COMMANDS, "  %s @ 0x%06x  %4uKB%s\n", part->label,
                     (unsigned)part->address, (unsigned)(part->size / 1024), tag);

            if (hasInfo)
                printfnl(SOURCE_COMMANDS, "    %s %s  built %s %s\n",
                         desc.project_name, desc.version, desc.date, desc.time);
            else
                printfnl(SOURCE_COMMANDS, "    (empty)\n");

            it = esp_partition_next(it);
        }
        esp_partition_iterator_release(it);
    }

    return 0;
}


int cmd_log(int argc, char **argv)
{
    // log to <path> — open file sink
    if (argc >= 3 && !strcasecmp(argv[1], "to")) {
        char path[64];
        normalize_path(path, sizeof(path), argv[2]);
        if (log_open(path)) {
            printfnl(SOURCE_COMMANDS, "Logging to %s\n", path);
        } else {
            printfnl(SOURCE_COMMANDS, "Failed to open %s\n", path);
        }
        return 0;
    }

    // log save <path> — dump ring buffer to file
    if (argc >= 3 && !strcasecmp(argv[1], "save")) {
        char path[64];
        normalize_path(path, sizeof(path), argv[2]);
        if (log_save(path)) {
            printfnl(SOURCE_COMMANDS, "Log saved to %s\n", path);
        } else {
            printfnl(SOURCE_COMMANDS, "Failed to save log to %s\n", path);
        }
        return 0;
    }

    // log close / log stop — close file sink
    if (argc >= 2 && (!strcasecmp(argv[1], "close") || !strcasecmp(argv[1], "stop"))) {
        log_close();
        printfnl(SOURCE_COMMANDS, "Log file closed\n");
        return 0;
    }

    // log (no args) — show ring buffer
    log_show();
    return 0;
}


int cmd_mqtt(int argc, char **argv)
{
    // mqtt broker <hostname>
    if (argc >= 3 && !strcasecmp(argv[1], "broker")) {
        strlcpy(config.mqtt_broker, argv[2], CONFIG_MAX_MQTT_BROKER);
        printfnl(SOURCE_COMMANDS, "MQTT broker set to \"%s\"\n", config.mqtt_broker);
        mqtt_force_disconnect();  // triggers reconnect to new broker
        return 0;
    }

    // mqtt port <number>
    if (argc >= 3 && !strcasecmp(argv[1], "port")) {
        config.mqtt_port = parse_int(argv[2]);
        printfnl(SOURCE_COMMANDS, "MQTT port set to %d\n", config.mqtt_port);
        mqtt_force_disconnect();  // triggers reconnect on new port
        return 0;
    }

    // mqtt enable
    if (argc >= 2 && !strcasecmp(argv[1], "enable")) {
        config.mqtt_enabled = true;
        mqtt_force_connect();
        printfnl(SOURCE_COMMANDS, "MQTT enabled\n");
        return 0;
    }

    // mqtt disable
    if (argc >= 2 && !strcasecmp(argv[1], "disable")) {
        config.mqtt_enabled = false;
        mqtt_force_disconnect();
        printfnl(SOURCE_COMMANDS, "MQTT disabled\n");
        return 0;
    }

    // mqtt connect
    if (argc >= 2 && !strcasecmp(argv[1], "connect")) {
        config.mqtt_enabled = true;
        mqtt_force_connect();
        printfnl(SOURCE_COMMANDS, "MQTT connect requested\n");
        return 0;
    }

    // mqtt disconnect
    if (argc >= 2 && !strcasecmp(argv[1], "disconnect")) {
        mqtt_force_disconnect();
        printfnl(SOURCE_COMMANDS, "MQTT disconnect requested\n");
        return 0;
    }

    // mqtt pub <topic> <payload>
    // Payload with spaces must be quoted: mqtt pub test/hello "Hello World"
    if (argc >= 3 && !strcasecmp(argv[1], "pub")) {
        const char *payload = (argc >= 4) ? argv[3] : "";
        int rc = mqtt_publish(argv[2], payload);
        if (rc == 0)
            printfnl(SOURCE_COMMANDS, "Published to %s\n", argv[2]);
        else
            printfnl(SOURCE_COMMANDS, "Publish failed (not connected?)\n");
        return rc;
    }

    // mqtt (no args) — show status
    printfnl(SOURCE_COMMANDS, "MQTT Status:\n");
    printfnl(SOURCE_COMMANDS, "  Enabled:    %s\n", config.mqtt_enabled ? "yes" : "no");
    printfnl(SOURCE_COMMANDS, "  Broker:     %s:%d\n", config.mqtt_broker, config.mqtt_port);
    printfnl(SOURCE_COMMANDS, "  State:      %s\n", mqtt_state_str());
    if (mqtt_connected()) {
        printfnl(SOURCE_COMMANDS, "  Uptime:     %lus\n", (unsigned long)mqtt_uptime_sec());
        printfnl(SOURCE_COMMANDS, "  TX packets: %lu\n", (unsigned long)mqtt_tx_count());
        printfnl(SOURCE_COMMANDS, "  RX packets: %lu\n", (unsigned long)mqtt_rx_count());
    }

    return 0;
}


int cmd_wifi(int argc, char **argv)
{
    // wifi enable
    if (argc >= 2 && !strcasecmp(argv[1], "enable")) {
        config.wifi_enabled = true;
        wifi_start(config.wifi_ssid, config.wifi_password, wifi_get_hostname());
        printfnl(SOURCE_COMMANDS, "WiFi enabled — connecting to \"%s\"\n", config.wifi_ssid);
        return 0;
    }

    // wifi disable
    if (argc >= 2 && !strcasecmp(argv[1], "disable")) {
        config.wifi_enabled = false;
        wifi_stop();
        printfnl(SOURCE_COMMANDS, "WiFi disabled\n");
        return 0;
    }

    // wifi ssid <name>
    if (argc >= 3 && !strcasecmp(argv[1], "ssid")) {
        strlcpy(config.wifi_ssid, argv[2], CONFIG_MAX_SSID);
        wifi_reconnect(config.wifi_ssid, config.wifi_password);
        printfnl(SOURCE_COMMANDS, "SSID set to \"%s\" — reconnecting\n", config.wifi_ssid);
        return 0;
    }

    // wifi password <psk>  /  wifi psk <psk>
    if (argc >= 3 && (!strcasecmp(argv[1], "password") || !strcasecmp(argv[1], "pass") || !strcasecmp(argv[1], "psk"))) {
        strlcpy(config.wifi_password, argv[2], CONFIG_MAX_PASSWORD);
        wifi_reconnect(config.wifi_ssid, config.wifi_password);
        printfnl(SOURCE_COMMANDS, "Password updated — reconnecting\n");
        return 0;
    }

    // wifi (no args) — show status
    getLock();
    Stream *out = getStream();
    out->println("WiFi Status:");
    out->printf("  Enabled:     %s\n", config.wifi_enabled ? "yes" : "no");
    out->printf("  Config SSID: %s\n", config.wifi_ssid);
    out->printf("  Status:      %s\n", wifi_state_str());

    if (wifi_is_connected()) {
        char buf[64];
        wifi_get_ssid(buf, sizeof(buf));
        out->printf("  SSID:        %s\n", buf);
        wifi_get_bssid_str(buf, sizeof(buf));
        out->printf("  BSSID:       %s\n", buf);
        out->printf("  Channel:     %d\n", wifi_get_channel());
        out->printf("  RSSI:        %d dBm\n", (int)wifi_get_rssi());
        wifi_get_ip_str(buf, sizeof(buf));
        out->printf("  IP:          %s\n", buf);
        wifi_get_gateway_str(buf, sizeof(buf));
        out->printf("  Gateway:     %s\n", buf);
        wifi_get_subnet_str(buf, sizeof(buf));
        out->printf("  Subnet:      %s\n", buf);
        wifi_get_dns_str(buf, sizeof(buf));
        out->printf("  DNS:         %s\n", buf);
        out->printf("  Hostname:    %s\n", wifi_get_hostname());
        uint32_t since = wifi_get_connected_since();
        if (since) {
            unsigned long sec = (uptime_ms() - since) / 1000;
            out->printf("  Connected:   %lud %02luh %02lum %02lus\n",
                sec / 86400, (sec % 86400) / 3600, (sec % 3600) / 60, sec % 60);
        }
#if LWIP_STATS
        out->printf("  IP  TX/RX:   %u / %u packets\n",
            (unsigned)lwip_stats.ip.xmit, (unsigned)lwip_stats.ip.recv);
        out->printf("  TCP TX/RX:   %u / %u segments\n",
            (unsigned)lwip_stats.tcp.xmit, (unsigned)lwip_stats.tcp.recv);
        out->printf("  UDP TX/RX:   %u / %u datagrams\n",
            (unsigned)lwip_stats.udp.xmit, (unsigned)lwip_stats.udp.recv);
#endif
        uint32_t tx, rx;
        if (wifi_get_byte_counts(&tx, &rx)) {
            if (tx < 1024 && rx < 1024)
                out->printf("  Bytes TX/RX: %u / %u\n", (unsigned)tx, (unsigned)rx);
            else if (tx < 1048576 && rx < 1048576)
                out->printf("  Bytes TX/RX: %.1f KB / %.1f KB\n", tx / 1024.0f, rx / 1024.0f);
            else
                out->printf("  Bytes TX/RX: %.1f MB / %.1f MB\n", tx / 1048576.0f, rx / 1048576.0f);
        }
    }

    uint8_t mac[6];
    wifi_get_mac(mac);
    out->printf("  MAC:         %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    out->printf("  TX power:    %.1f dBm\n", wifi_get_tx_power_dbm() / 4.0f);
    releaseLock();

    return 0;
}


// --- GPIO pin name table (board-specific) ---
struct PinInfo { int pin; const char *name; };

static const PinInfo pin_table[] = {
#ifdef BOARD_CONEZ_V0_1
    {  0, "BOOT/USR" },
    {  1, "ADC_BAT" },
    {  2, "ADC_SOLAR" },
    {  3, "(reserved)" },
    {  4, "PSR_MISO" },
    {  5, "PSR_CE" },
    {  6, "PSR_SCK" },
    {  7, "PSR_MOSI" },
    {  8, "LORA_CS" },
    {  9, "LORA_SCK" },
    { 10, "LORA_MOSI" },
    { 11, "LORA_MISO" },
    { 12, "LORA_RST" },
    { 13, "LORA_BUSY" },
    { 14, "LORA_DIO1" },
    { 15, "EXT1" },
    { 16, "EXT2" },
    { 17, "I2C_SDA" },
    { 18, "I2C_SCL" },
    { 19, "USB_N" },
    { 20, "USB_P" },
    { 21, "SOLAR_PWM" },
    { 33, "PWR_SW" },
    { 34, "PWR_OFF" },
    { 35, "RGB4" },
    { 36, "RGB3" },
    { 37, "RGB2" },
    { 38, "RGB1" },
    { 40, "LED" },
    { 41, "IMU_INT" },
    { 42, "GPS_PPS" },
    { 43, "GPS_TX" },
    { 44, "GPS_RX" },
    { 47, "LOAD_ON" },
    { 48, "BUZZER" },
#elif defined(BOARD_HELTEC_LORA32_V3)
    {  0, "BUTTON" },
    {  1, "ADC_BAT" },
    {  8, "LORA_CS" },
    {  9, "LORA_SCK" },
    { 10, "LORA_MOSI" },
    { 11, "LORA_MISO" },
    { 12, "LORA_RST" },
    { 13, "LORA_BUSY" },
    { 14, "LORA_DIO1" },
    { 17, "I2C_SDA" },
    { 18, "I2C_SCL" },
    { 19, "USB_N" },
    { 20, "USB_P" },
    { 21, "OLED_RST" },
    { 35, "LED" },
    { 36, "VEXT" },
    { 43, "USB_TX" },
    { 44, "USB_RX" },
#endif
    { -1, NULL }
};

static const char *pin_name_lookup(int gpio)
{
    for (const PinInfo *p = pin_table; p->pin >= 0; p++)
        if (p->pin == gpio) return p->name;
    return "";
}

// Returns true if the pin is a valid ESP32-S3 GPIO (0-21, 33-48)
static bool gpio_valid_pin(int pin)
{
    return (pin >= 0 && pin <= 21) || (pin >= 33 && pin <= 48);
}

// Returns true if the pin is reserved for critical hardware and should not be reconfigured
static bool gpio_is_reserved(int pin)
{
    // USB
    if (pin == 19 || pin == 20) return true;
#ifdef BOARD_CONEZ_V0_1
    // PSRAM SPI
    if (pin >= 4 && pin <= 7) return true;
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // GPS UART
    if (pin == 43 || pin == 44) return true;
    // GPS PPS
    if (pin == 42) return true;
    // I2C
    if (pin == 17 || pin == 18) return true;
#elif defined(BOARD_HELTEC_LORA32_V3)
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // I2C / OLED
    if (pin == 17 || pin == 18 || pin == 21) return true;
#endif
    return false;
}

static void gpio_show_all(void)
{
    printfnl(SOURCE_COMMANDS, "GPIO  Val  Dir  Pull      Function\n");
    printfnl(SOURCE_COMMANDS, "----  ---  ---  --------  ----------\n");

    uint32_t out_en_lo = REG_READ(GPIO_ENABLE_REG);
    uint32_t out_en_hi = REG_READ(GPIO_ENABLE1_REG);

    for (int i = 0; i <= 48; i++) {
        // ESP32-S3 has no GPIO 22-32
        if (i >= 22 && i <= 32) continue;

        int level = gpio_get_level((gpio_num_t)i);

        bool is_output;
        if (i < 32)
            is_output = (out_en_lo >> i) & 1;
        else
            is_output = (out_en_hi >> (i - 32)) & 1;

        uint32_t iomux_reg = REG_READ(GPIO_PIN_MUX_REG[i]);
        bool pull_up   = (iomux_reg >> 8) & 1;
        bool pull_down = (iomux_reg >> 7) & 1;

        const char *pull_str;
        if (pull_up && pull_down) pull_str = "UP+DOWN";
        else if (pull_up)        pull_str = "UP";
        else if (pull_down)      pull_str = "DOWN";
        else                     pull_str = "-";

        const char *name = pin_name_lookup(i);

        printfnl(SOURCE_COMMANDS, " %2d    %d   %s  %-8s  %s\n",
            i, level,
            is_output ? "OUT" : "IN ",
            pull_str, name);
    }
}

int cmd_gpio(int argc, char **argv)
{
    // "gpio" — show all pin states
    if (argc == 1) {
        gpio_show_all();
        return 0;
    }

    // "gpio set <pin> <0|1>" — set output level
    if (argc == 4 && strcasecmp(argv[1], "set") == 0) {
        int pin = parse_int(argv[2]);
        int val = parse_int(argv[3]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, "Invalid GPIO pin %d\n", pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, "GPIO %d is reserved (use 'gpio' to see pin assignments)\n", pin);
            return -1;
        }
        if (val != 0 && val != 1) {
            printfnl(SOURCE_COMMANDS, "Value must be 0 or 1\n");
            return -1;
        }
        gpio_set_level((gpio_num_t)pin, val);
        printfnl(SOURCE_COMMANDS, "GPIO %d -> %d\n", pin, val);
        return 0;
    }

    // "gpio out <pin> <0|1>" — configure as output and set value
    if (argc == 4 && strcasecmp(argv[1], "out") == 0) {
        int pin = parse_int(argv[2]);
        int val = parse_int(argv[3]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, "Invalid GPIO pin %d\n", pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, "GPIO %d is reserved (use 'gpio' to see pin assignments)\n", pin);
            return -1;
        }
        if (val != 0 && val != 1) {
            printfnl(SOURCE_COMMANDS, "Value must be 0 or 1\n");
            return -1;
        }
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)pin, val);
        printfnl(SOURCE_COMMANDS, "GPIO %d -> OUTPUT %d\n", pin, val);
        return 0;
    }

    // "gpio in <pin> [pull]" — configure as input with optional pull
    //   pull: up, down, none (default: none)
    if ((argc == 3 || argc == 4) && strcasecmp(argv[1], "in") == 0) {
        int pin = parse_int(argv[2]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, "Invalid GPIO pin %d\n", pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, "GPIO %d is reserved (use 'gpio' to see pin assignments)\n", pin);
            return -1;
        }
        gpio_pull_mode_t pull = GPIO_FLOATING;
        const char *pull_name = "none";
        if (argc == 4) {
            if (strcasecmp(argv[3], "up") == 0) {
                pull = GPIO_PULLUP_ONLY;
                pull_name = "pull-up";
            } else if (strcasecmp(argv[3], "down") == 0) {
                pull = GPIO_PULLDOWN_ONLY;
                pull_name = "pull-down";
            } else if (strcasecmp(argv[3], "none") == 0) {
                pull = GPIO_FLOATING;
                pull_name = "none";
            } else {
                printfnl(SOURCE_COMMANDS, "Pull mode must be: up, down, or none\n");
                return -1;
            }
        }
        gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)pin, pull);
        printfnl(SOURCE_COMMANDS, "GPIO %d -> INPUT (%s)\n", pin, pull_name);
        return 0;
    }

    // "gpio read <pin>" — read a single pin
    if (argc == 3 && strcasecmp(argv[1], "read") == 0) {
        int pin = parse_int(argv[2]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, "Invalid GPIO pin %d\n", pin);
            return -1;
        }
        printfnl(SOURCE_COMMANDS, "GPIO %d = %d\n", pin, gpio_get_level((gpio_num_t)pin));
        return 0;
    }

    printfnl(SOURCE_COMMANDS, "Usage:\n");
    printfnl(SOURCE_COMMANDS, "  gpio              Show all pin states\n");
    printfnl(SOURCE_COMMANDS, "  gpio set <pin> <0|1>      Set output level\n");
    printfnl(SOURCE_COMMANDS, "  gpio out <pin> <0|1>      Set as output with value\n");
    printfnl(SOURCE_COMMANDS, "  gpio in  <pin> [up|down|none]  Set as input\n");
    printfnl(SOURCE_COMMANDS, "  gpio read <pin>           Read single pin\n");
    return -1;
}


// Compute effective timezone offset in hours (standard + DST if applicable)
static int effective_tz_offset(int year, int month, int day)
{
    int tz = config.timezone;
    if (config.auto_dst && is_us_dst(year, month, day))
        tz += 1;
    return tz;
}

// Format a timezone label like "UTC-7" or "UTC+0"
static const char *tz_label(int tz_hours)
{
    static char buf[16];
    if (tz_hours >= 0)
        snprintf(buf, sizeof(buf), "UTC+%d", tz_hours);
    else
        snprintf(buf, sizeof(buf), "UTC%d", tz_hours);
    return buf;
}

static void gps_show_status(void)
{
#ifdef BOARD_HAS_GPS
    static const char *fix_names[] = { "Unknown", "No Fix", "2D", "3D" };
    int ft = get_fix_type();
    const char *fix_str = (ft >= 0 && ft <= 3) ? fix_names[ft] : "Unknown";
    printfnl(SOURCE_COMMANDS, "GPS Status:\n");
    printfnl(SOURCE_COMMANDS, "  Fix:        %s (%s)\n", get_gpsstatus() ? "Yes" : "No", fix_str);
    printfnl(SOURCE_COMMANDS, "  Satellites: %d\n", get_satellites());
    printfnl(SOURCE_COMMANDS, "  HDOP:       %.2f\n", get_hdop() / 100.0);
    printfnl(SOURCE_COMMANDS, "  VDOP:       %.2f\n", get_vdop());
    printfnl(SOURCE_COMMANDS, "  PDOP:       %.2f\n", get_pdop());
    printfnl(SOURCE_COMMANDS, "  Position:   %.6f, %.6f\n", get_lat(), get_lon());
    float alt_m = get_alt();
    printfnl(SOURCE_COMMANDS, "  Altitude:   %.0f m (%.0f ft)\n", alt_m, alt_m * 3.28084f);
    float spd_mps = get_speed();
    printfnl(SOURCE_COMMANDS, "  Speed:      %.1f m/s (%.1f mph)\n", spd_mps, spd_mps * 2.23694f);
    printfnl(SOURCE_COMMANDS, "  Direction:  %.1f deg\n", get_dir());
    // Show local time (UTC + timezone + DST)
    {
        int utc_y = get_year(), utc_m = get_month(), utc_d = get_day();
        int tz = effective_tz_offset(utc_y, utc_m, utc_d);
        uint64_t epoch = get_epoch_ms();
        time_t local_t = (time_t)(epoch / 1000) + tz * 3600;
        struct tm ltm;
        gmtime_r(&local_t, &ltm);
        printfnl(SOURCE_COMMANDS, "  Time:       %02d:%02d:%02d  %04d-%02d-%02d (%s)\n",
            ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
            ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
            tz_label(tz));
    }
    static const char *src_names[] = { "None", "NTP", "GPS+PPS" };
    uint8_t ts = get_time_source();
    const char *tsn = (ts == 0 && get_time_valid()) ? "Build" : src_names[ts < 3 ? ts : 0];
    printfnl(SOURCE_COMMANDS, "  Time src:   %s\n", tsn);
    uint32_t pps_age = get_pps_age_ms();
    if (pps_age == UINT32_MAX)
        printfnl(SOURCE_COMMANDS, "  PPS:        No (never received)\n");
    else
        printfnl(SOURCE_COMMANDS, "  PPS:        %s (%lu ms ago, %lu pulses)\n",
            get_pps() ? "High" : "Low", (unsigned long)pps_age, (unsigned long)get_pps_count());
#else
    printfnl(SOURCE_COMMANDS, "GPS not available on this board\n");
#endif
}


static void gps_show_usage(void)
{
    printfnl(SOURCE_COMMANDS, "Usage:\n");
    printfnl(SOURCE_COMMANDS, "  gps                        Show GPS status\n");
    printfnl(SOURCE_COMMANDS, "  gps info                   Query module firmware/hardware\n");
    printfnl(SOURCE_COMMANDS, "  gps set baud <rate>        Set baud (4800/9600/19200/38400/57600/115200)\n");
    printfnl(SOURCE_COMMANDS, "  gps set rate <hz>          Set update rate (1/2/4/5/10)\n");
    printfnl(SOURCE_COMMANDS, "  gps set mode <mode>        Set constellation (gps/bds/glonass or combos)\n");
    printfnl(SOURCE_COMMANDS, "  gps set nmea <sentences>   Enable NMEA sentences (e.g. gga,rmc,gsa)\n");
    printfnl(SOURCE_COMMANDS, "  gps save                   Save config to module flash\n");
    printfnl(SOURCE_COMMANDS, "  gps restart <type>         Restart (hot/warm/cold/factory)\n");
    printfnl(SOURCE_COMMANDS, "  gps send <body>            Send raw NMEA (auto-checksum)\n");
}


int cmd_gps(int argc, char **argv)
{
    // No subcommand — show status
    if (argc < 2) {
        gps_show_status();
        return 0;
    }

#ifndef BOARD_HAS_GPS
    printfnl(SOURCE_COMMANDS, "GPS not available on this board\n");
    return -1;
#else

    // --- gps info: query module firmware and hardware ---
    if (strcasecmp(argv[1], "info") == 0) {
        printfnl(SOURCE_COMMANDS, "Querying GPS module info (enable 'debug gps_raw' to see response)...\n");
        gps_send_nmea("PCAS06,0");  // firmware version
        gps_send_nmea("PCAS06,1");  // hardware model
        return 0;
    }

    // --- gps set <subcommand> ---
    if (strcasecmp(argv[1], "set") == 0) {
        if (argc < 3) {
            gps_show_usage();
            return -1;
        }

        // --- gps set baud <rate> ---
        if (strcasecmp(argv[2], "baud") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, "Usage: gps set baud <4800|9600|19200|38400|57600|115200>\n");
                return -1;
            }
            int rate = parse_int(argv[3]);
            int code = -1;
            switch (rate) {
                case 4800:   code = 0; break;
                case 9600:   code = 1; break;
                case 19200:  code = 2; break;
                case 38400:  code = 3; break;
                case 57600:  code = 4; break;
                case 115200: code = 5; break;
            }
            if (code < 0) {
                printfnl(SOURCE_COMMANDS, "Invalid baud rate. Use: 4800/9600/19200/38400/57600/115200\n");
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS01,%d", code);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, "Baud set to %d (use 'gps save' to persist)\n", rate);
            printfnl(SOURCE_COMMANDS, "Note: firmware still expects 9600. Reboot to reconnect.\n");
            return 0;
        }

        // --- gps set rate <hz> ---
        if (strcasecmp(argv[2], "rate") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, "Usage: gps set rate <1|2|4|5|10>\n");
                return -1;
            }
            int hz = parse_int(argv[3]);
            int ms = -1;
            switch (hz) {
                case 1:  ms = 1000; break;
                case 2:  ms = 500;  break;
                case 4:  ms = 250;  break;
                case 5:  ms = 200;  break;
                case 10: ms = 100;  break;
            }
            if (ms < 0) {
                printfnl(SOURCE_COMMANDS, "Invalid rate. Use: 1, 2, 4, 5, or 10 Hz\n");
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS02,%d", ms);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, "Update rate set to %d Hz (%d ms)\n", hz, ms);
            return 0;
        }

        // --- gps set mode <constellation> ---
        if (strcasecmp(argv[2], "mode") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, "Usage: gps set mode <gps|bds|glonass|gps+bds|gps+glonass|bds+glonass|all>\n");
                return -1;
            }
            int mode = -1;
            if (strcasecmp(argv[3], "gps") == 0)           mode = 1;
            else if (strcasecmp(argv[3], "bds") == 0)       mode = 2;
            else if (strcasecmp(argv[3], "gps+bds") == 0)   mode = 3;
            else if (strcasecmp(argv[3], "glonass") == 0)    mode = 4;
            else if (strcasecmp(argv[3], "gps+glonass") == 0) mode = 5;
            else if (strcasecmp(argv[3], "bds+glonass") == 0) mode = 6;
            else if (strcasecmp(argv[3], "all") == 0)        mode = 7;

            if (mode < 0) {
                printfnl(SOURCE_COMMANDS, "Invalid mode. Use: gps, bds, glonass, gps+bds, gps+glonass, bds+glonass, all\n");
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS04,%d", mode);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, "Constellation mode set to %d\n", mode);
            return 0;
        }

        // --- gps set nmea <sentences> ---
        // Enables listed NMEA sentences at 1Hz, disables the rest
        // e.g. "gps set nmea gga,rmc,gsa"
        if (strcasecmp(argv[2], "nmea") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, "Usage: gps set nmea <gga,gll,gsa,gsv,rmc,vtg,zda,...>\n");
                printfnl(SOURCE_COMMANDS, "  Enables listed sentences at 1/fix, disables others\n");
                printfnl(SOURCE_COMMANDS, "  Slots: gga,gll,gsa,gsv,rmc,vtg,zda,ant,dhv,lps,,,utc,gst\n");
                return -1;
            }
            // PCAS03 field order: GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,DHV,LPS,res,res,UTC,GST,res,res,res,TIM
            static const char *names[] = {
                "gga","gll","gsa","gsv","rmc","vtg","zda","ant","dhv","lps",
                NULL, NULL, "utc", "gst", NULL, NULL, NULL, "tim"
            };
            int fields[18] = {0};

            // Parse comma-separated list from argv[3]
            char list[64];
            strncpy(list, argv[3], sizeof(list) - 1);
            list[sizeof(list) - 1] = '\0';
            char *saveptr;
            char *tok = strtok_r(list, ",", &saveptr);
            while (tok) {
                bool found = false;
                for (int i = 0; i < 18; i++) {
                    if (names[i] && strcasecmp(tok, names[i]) == 0) {
                        fields[i] = 1;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    printfnl(SOURCE_COMMANDS, "  Unknown sentence: %s (ignored)\n", tok);
                tok = strtok_r(NULL, ",", &saveptr);
            }

            char buf[80];
            snprintf(buf, sizeof(buf),
                "PCAS03,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                fields[0], fields[1], fields[2], fields[3], fields[4], fields[5],
                fields[6], fields[7], fields[8], fields[9], fields[10], fields[11],
                fields[12], fields[13], fields[14], fields[15], fields[16], fields[17]);
            gps_send_nmea(buf);
            return 0;
        }

        gps_show_usage();
        return -1;
    }

    // --- gps save ---
    if (strcasecmp(argv[1], "save") == 0) {
        gps_send_nmea("PCAS00");
        printfnl(SOURCE_COMMANDS, "Configuration saved to GPS module flash\n");
        return 0;
    }

    // --- gps restart <type> ---
    if (strcasecmp(argv[1], "restart") == 0) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, "Usage: gps restart <hot|warm|cold|factory>\n");
            return -1;
        }
        int rs = -1;
        if (strcasecmp(argv[2], "hot") == 0)      rs = 0;
        else if (strcasecmp(argv[2], "warm") == 0)  rs = 1;
        else if (strcasecmp(argv[2], "cold") == 0)  rs = 2;
        else if (strcasecmp(argv[2], "factory") == 0) rs = 3;

        if (rs < 0) {
            printfnl(SOURCE_COMMANDS, "Invalid restart type. Use: hot, warm, cold, factory\n");
            return -1;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "PCAS10,%d", rs);
        gps_send_nmea(buf);
        printfnl(SOURCE_COMMANDS, "GPS module restarting (%s)\n", argv[2]);
        return 0;
    }

    // --- gps send <raw body> ---
    // Send arbitrary NMEA body with auto-checksum, e.g. "gps send PCAS06,0"
    if (strcasecmp(argv[1], "send") == 0) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, "Usage: gps send <NMEA body>  (e.g. PCAS06,0)\n");
            return -1;
        }
        // Rejoin remaining args with spaces (in case user typed spaces)
        char buf[80];
        buf[0] = '\0';
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        }
        gps_send_nmea(buf);
        return 0;
    }

    gps_show_usage();
    return -1;
#endif
}


int cmd_lora(int argc, char **argv)
{
#ifdef BOARD_HAS_LORA
    if (argc >= 3)
    {
        const char *sub = argv[1];
        const char *val = argv[2];

        if (strcasecmp(sub, "freq") == 0)
        {
            float freq = atof(val);
            config.lora_frequency = freq;
            int rc = lora_set_frequency(freq);
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error setting frequency (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "Frequency set to %.3f MHz\n", freq);
            return 0;
        }
        else if (strcasecmp(sub, "power") == 0)
        {
            int power = parse_int(val);
            config.lora_tx_power = power;
            int rc = lora_set_tx_power(power);
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error setting TX power (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "TX power set to %d dBm\n", power);
            return 0;
        }
        else if (strcasecmp(sub, "bw") == 0)
        {
            if (lora_is_fsk()) {
                printfnl(SOURCE_COMMANDS, "Bandwidth not available in FSK mode\n");
                return 0;
            }
            float bw = atof(val);
            config.lora_bandwidth = bw;
            int rc = lora_set_bandwidth(bw);
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error setting bandwidth (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "Bandwidth set to %.1f kHz\n", bw);
            return 0;
        }
        else if (strcasecmp(sub, "sf") == 0)
        {
            if (lora_is_fsk()) {
                printfnl(SOURCE_COMMANDS, "SF not available in FSK mode\n");
                return 0;
            }
            int sf = parse_int(val);
            config.lora_sf = sf;
            int rc = lora_set_sf(sf);
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error setting SF (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "SF set to %d\n", sf);
            return 0;
        }
        else if (strcasecmp(sub, "cr") == 0)
        {
            if (lora_is_fsk()) {
                printfnl(SOURCE_COMMANDS, "CR not available in FSK mode\n");
                return 0;
            }
            int cr = parse_int(val);
            config.lora_cr = cr;
            int rc = lora_set_cr(cr);
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error setting CR (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "CR set to 4/%d\n", cr);
            return 0;
        }
        else if (strcasecmp(sub, "mode") == 0)
        {
            if (strcasecmp(val, "lora") != 0 && strcasecmp(val, "fsk") != 0) {
                printfnl(SOURCE_COMMANDS, "Invalid mode '%s' (use lora or fsk)\n", val);
                return 0;
            }
            strncpy(config.lora_rf_mode, val, sizeof(config.lora_rf_mode) - 1);
            config.lora_rf_mode[sizeof(config.lora_rf_mode) - 1] = '\0';
            int rc = lora_reinit();
            if (rc != 0)
                printfnl(SOURCE_COMMANDS, "Error switching mode (code %d)\n", rc);
            else
                printfnl(SOURCE_COMMANDS, "Mode set to %s\n", lora_get_mode());
            return 0;
        }
    }

    printfnl(SOURCE_COMMANDS, "LoRa Radio:\n");
    printfnl(SOURCE_COMMANDS, "  Mode:      %s\n", lora_get_mode());
    printfnl(SOURCE_COMMANDS, "  Frequency: %.3f MHz\n", lora_get_frequency());
    printfnl(SOURCE_COMMANDS, "  TX Power:  %d dBm\n", config.lora_tx_power);

    if (lora_is_fsk())
    {
        printfnl(SOURCE_COMMANDS, "  Bit Rate:  %.1f kbps\n", lora_get_bitrate());
        printfnl(SOURCE_COMMANDS, "  Freq Dev:  %.1f kHz\n", lora_get_freqdev());
        printfnl(SOURCE_COMMANDS, "  RX BW:     %.1f kHz\n", lora_get_rxbw());
        printfnl(SOURCE_COMMANDS, "  Preamble:  %d\n", config.lora_preamble);

        static const char *shaping_names[] = { "None", "BT0.3", "BT0.5", "BT0.7", "BT1.0" };
        int si = config.fsk_shaping;
        if (si < 0 || si > 4) si = 0;
        printfnl(SOURCE_COMMANDS, "  Shaping:   %s\n", shaping_names[si]);
        printfnl(SOURCE_COMMANDS, "  Whitening: %s\n", config.fsk_whitening ? "on" : "off");
        printfnl(SOURCE_COMMANDS, "  Sync Word: %s\n", config.fsk_syncword);

        const char *crc_names[] = { "off", "1-byte", "2-byte" };
        int ci = config.fsk_crc;
        if (ci < 0 || ci > 2) ci = 0;
        printfnl(SOURCE_COMMANDS, "  CRC:       %s\n", crc_names[ci]);
    }
    else
    {
        printfnl(SOURCE_COMMANDS, "  Bandwidth: %.1f kHz\n", lora_get_bandwidth());
        printfnl(SOURCE_COMMANDS, "  SF:        %d\n", lora_get_sf());
        printfnl(SOURCE_COMMANDS, "  CR:        4/%d\n", config.lora_cr);
        printfnl(SOURCE_COMMANDS, "  Preamble:  %d\n", config.lora_preamble);
        printfnl(SOURCE_COMMANDS, "  Sync Word: 0x%02X\n", config.lora_sync_word);
    }

    float dr = lora_get_datarate();
    if (dr >= 1000.0f)
        printfnl(SOURCE_COMMANDS, "  Data Rate: %.2f kbps\n", dr / 1000.0f);
    else
        printfnl(SOURCE_COMMANDS, "  Data Rate: %.0f bps\n", dr);
    printfnl(SOURCE_COMMANDS, "  TX Pkts:   %lu\n", (unsigned long)lora_get_tx_count());
    printfnl(SOURCE_COMMANDS, "  RX Pkts:   %lu\n", (unsigned long)lora_get_rx_count());
    printfnl(SOURCE_COMMANDS, "  Last RSSI: %.1f dBm\n", lora_get_rssi());
    printfnl(SOURCE_COMMANDS, "  Last SNR:  %.1f dB\n", lora_get_snr());
#else
    printfnl(SOURCE_COMMANDS, "LoRa not available on this board\n");
#endif
    return 0;
}


int cmd_sensors(int argc, char **argv)
{
    printfnl(SOURCE_COMMANDS, "Sensors:\n");

#ifdef BOARD_HAS_IMU
    printfnl(SOURCE_COMMANDS, "  IMU:         %s\n", imuAvailable() ? "Available" : "Not detected");
    if (imuAvailable())
    {
        printfnl(SOURCE_COMMANDS, "  Roll:        %.1f deg\n", getRoll());
        printfnl(SOURCE_COMMANDS, "  Pitch:       %.1f deg\n", getPitch());
        printfnl(SOURCE_COMMANDS, "  Yaw:         %.1f deg\n", getYaw());
        printfnl(SOURCE_COMMANDS, "  Accel:       %.2f, %.2f, %.2f g\n", getAccX(), getAccY(), getAccZ());
    }
#else
    printfnl(SOURCE_COMMANDS, "  IMU:         Not available on this board\n");
#endif

    printfnl(SOURCE_COMMANDS, "  Temperature: %.1f C\n", getTemp());
    printfnl(SOURCE_COMMANDS, "  Battery:     %.2f V\n", bat_voltage());

#ifdef BOARD_HAS_POWER_MGMT
    printfnl(SOURCE_COMMANDS, "  Solar:       %.2f V\n", solar_voltage());
#endif

    // ADC1 channels (GPIO 1-10 on ESP32-S3)
    printfnl(SOURCE_COMMANDS, "\nADC1 (GPIO 1-10):\n");
    for (int pin = 1; pin <= 10; pin++) {
        int mv = analogReadMilliVolts(pin);
        const char *name = pin_name_lookup(pin);
        if (name[0])
            printfnl(SOURCE_COMMANDS, "  GPIO %2d: %4d mV  (%s)\n", pin, mv, name);
        else
            printfnl(SOURCE_COMMANDS, "  GPIO %2d: %4d mV\n", pin, mv);
    }

    return 0;
}


int cmd_time(int argc, char **argv)
{
    static const char *dayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

    if (get_time_valid()) {
        uint64_t epoch = get_epoch_ms();

        // Compute local time from epoch + timezone + DST
        int utc_y = get_year(), utc_m = get_month(), utc_d = get_day();
        int tz = effective_tz_offset(utc_y, utc_m, utc_d);
        time_t local_t = (time_t)(epoch / 1000) + tz * 3600;
        struct tm ltm;
        gmtime_r(&local_t, &ltm);
        int dow = ltm.tm_wday;
        if (dow < 0 || dow > 6) dow = 0;

        printfnl(SOURCE_COMMANDS, "Time:   %04d-%02d-%02d %02d:%02d:%02d %s (%s)\n",
            ltm.tm_year + 1900, ltm.tm_mon + 1, ltm.tm_mday,
            ltm.tm_hour, ltm.tm_min, ltm.tm_sec,
            tz_label(tz), dayNames[dow]);

        printfnl(SOURCE_COMMANDS, "Epoch:  %lu%03lu ms\n",
            (unsigned long)(epoch / 1000), (unsigned long)(epoch % 1000));
    } else {
        printfnl(SOURCE_COMMANDS, "Time:   not available\n");
    }

    // Show time source
    uint8_t ts = get_time_source();
    const char *src = "none";
    if (ts == 2)      src = "GPS+PPS";
    else if (ts == 1) src = "NTP";
    else if (get_time_valid()) src = "build";
    printfnl(SOURCE_COMMANDS, "Source: %s\n", src);

    // NTP line with sync age
    uint32_t ntp_sync = get_ntp_last_sync_ms();
    if (ntp_sync) {
        unsigned long ago = (uptime_ms() - ntp_sync) / 1000;
        printfnl(SOURCE_COMMANDS, "NTP:    %s  (synced %lus ago)\n", config.ntp_server, ago);
    } else {
        printfnl(SOURCE_COMMANDS, "NTP:    %s  (never synced)\n", config.ntp_server);
    }

#ifdef BOARD_HAS_GPS
    printfnl(SOURCE_COMMANDS, "GPS fix: %s  Sats: %d\n", get_gpsstatus() ? "Yes" : "No", get_satellites());
#endif

    // Uptime
    unsigned long ms = uptime_ms();
    unsigned long totalSec = ms / 1000;
    unsigned int days  = totalSec / 86400;
    unsigned int hours = (totalSec % 86400) / 3600;
    unsigned int mins  = (totalSec % 3600) / 60;
    unsigned int secs  = totalSec % 60;
    printfnl(SOURCE_COMMANDS, "Uptime: %ud %02uh %02um %02us\n", days, hours, mins, secs);

    return 0;
}


static bool parse_color(const char *s, CRGB *out)
{
    if (*s == '#') s++;
    if (strlen(s) != 6) return false;
    char *end;
    unsigned long v = strtoul(s, &end, 16);
    if (*end) return false;
    *out = CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

int cmd_led(int argc, char **argv)
{
#ifdef BOARD_HAS_RGB_LEDS
    CRGB *bufs[]  = { leds1, leds2, leds3, leds4 };
    int   counts[] = { config.led_count1, config.led_count2,
                       config.led_count3, config.led_count4 };

    // led count <ch> <n> — resize a channel
    if (argc >= 4 && !strcasecmp(argv[1], "count")) {
        int ch = parse_int(argv[2]);
        int n  = parse_int(argv[3]);
        if (ch < 1 || ch > 4) {
            printfnl(SOURCE_COMMANDS, "Invalid channel %d (1-4)\n", ch);
            return 1;
        }
        if (n < 0) {
            printfnl(SOURCE_COMMANDS, "Count must be >= 0\n");
            return 1;
        }
        int rc = led_resize_channel(ch, n);
        if (rc != 0)
            printfnl(SOURCE_COMMANDS, "Failed to resize channel %d\n", ch);
        else
            printfnl(SOURCE_COMMANDS, "Channel %d set to %d LEDs\n", ch, n);
        return 0;
    }

    // led clear — all channels to black
    if (argc >= 2 && !strcasecmp(argv[1], "clear")) {
        for (int ch = 0; ch < 4; ch++)
            if (bufs[ch]) { for (int j = 0; j < counts[ch]; j++) bufs[ch][j] = CRGB::Black; }
        led_show();
        printfnl(SOURCE_COMMANDS, "All LEDs cleared\n");
        return 0;
    }

    // led set <ch> <index|start-end|all> <#RRGGBB>
    if (argc >= 2 && !strcasecmp(argv[1], "set")) {
        if (argc < 5) {
            printfnl(SOURCE_COMMANDS, "Usage: led set <ch> <index|start-end|all> <#RRGGBB>\n");
            return 1;
        }
        int ch = parse_int(argv[2]);
        if (ch < 1 || ch > 4 || !bufs[ch - 1]) {
            printfnl(SOURCE_COMMANDS, "Invalid channel %d\n", ch);
            return 1;
        }
        CRGB color;
        if (!parse_color(argv[4], &color)) {
            printfnl(SOURCE_COMMANDS, "Invalid color: %s (use #RRGGBB)\n", argv[4]);
            return 1;
        }
        int n = counts[ch - 1];
        int start, end;
        if (!strcasecmp(argv[3], "all")) {
            start = 0; end = n - 1;
        } else {
            char *dash = strchr(argv[3], '-');
            if (dash) {
                *dash = '\0';
                start = parse_int(argv[3]);
                end   = parse_int(dash + 1);
            } else {
                start = end = parse_int(argv[3]);
            }
        }
        if (start < 0 || end >= n || start > end) {
            printfnl(SOURCE_COMMANDS, "Index out of range (0-%d)\n", n - 1);
            return 1;
        }
        for (int i = start; i <= end; i++)
            bufs[ch - 1][i] = color;
        led_show();
        printfnl(SOURCE_COMMANDS, "Ch%d [%d-%d] = #%02X%02X%02X\n",
                 ch, start, end, color.r, color.g, color.b);
        return 0;
    }

    // led (no args) — show config + RGB values
    printfnl(SOURCE_COMMANDS, "LED Config:\n");
    for (int ch = 0; ch < 4; ch++) {
        printfnl(SOURCE_COMMANDS, "  Strip %d: %d LEDs\n", ch + 1, counts[ch]);
    }
    for (int ch = 0; ch < 4; ch++) {
        if (!bufs[ch] || counts[ch] == 0) continue;
        if (getAnsiEnabled()) {
            getLock();
            Stream *out = getStream();
            out->printf("\nCh%d: [", ch + 1);
            uint8_t pr = 0, pg = 0, pb = 0;
            bool first = true;
            for (int i = 0; i < counts[ch]; i++) {
                CRGB c = bufs[ch][i];
                if (first || c.r != pr || c.g != pg || c.b != pb) {
                    out->printf("\033[38;2;%d;%d;%dm", c.r, c.g, c.b);
                    pr = c.r; pg = c.g; pb = c.b;
                    first = false;
                }
                out->print("\xe2\x96\x88");  // U+2588 FULL BLOCK (UTF-8)
            }
            out->print("\033[0m]\n");
            releaseLock();
        } else {
            printfnl(SOURCE_COMMANDS, "\nCh%d:\n", ch + 1);
        }
        for (int i = 0; i < counts[ch]; i++) {
            if (i % 8 == 0)
                printfnl(SOURCE_COMMANDS, "  %3d:", i);
            CRGB c = bufs[ch][i];
            printfnl(SOURCE_COMMANDS, " #%02X%02X%02X", c.r, c.g, c.b);
            if (i % 8 == 7 || i == counts[ch] - 1)
                printfnl(SOURCE_COMMANDS, "\n");
        }
    }
#else
    printfnl(SOURCE_COMMANDS, "RGB LEDs not available on this board\n");
#endif
    return 0;
}


int cmd_art( int argc, char **argv )
{
    if (!getAnsiEnabled()) {
        printfnl(SOURCE_COMMANDS, "Requires ANSI mode (color on)\n");
        return 1;
    }
    getLock();
    Stream *out = getStream();
    out->print(
        "\n"
        "\033[38;5;208m"
        "            ▄\n"
        "           ███\n"
        "          █████\n"
        "\033[97m"
        "         ███████\n"
        "\033[38;5;208m"
        "        █████████\n"
        "       ███████████\n"
        "\033[97m"
        "      █████████████\n"
        "\033[38;5;208m"
        "     ███████████████\n"
        "    █████████████████\n"
        "   ███████████████████\n"
        "  █████████████████████\n"
        "\033[38;5;240m"
        " ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄\n"
        "\033[0m"
        "\n"
        "      Is it art...?\n"
        "\n"
    );
    releaseLock();
    return 0;
}


// Helper: draw N copies of a UTF-8 character
static void wa_repeat(Stream *out, const char *ch, int n)
{
    for (int i = 0; i < n; i++) out->print(ch);
}

int cmd_winamp(int argc, char **argv)
{
    if (!getAnsiEnabled()) {
        printfnl(SOURCE_COMMANDS, "Requires ANSI mode (color on)\n");
        return 1;
    }
    setInteractive(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    // 40 spectrum bars, heights 0-8
    const int NBARS = 40, SROWS = 8;
    int bars[NBARS];
    for (int i = 0; i < NBARS; i++) bars[i] = esp_random() % (SROWS + 1);

    // Spectrum row colors: green -> yellow -> orange -> red (bottom to top)
    static const uint8_t spc[8][3] = {
        {0,170,0}, {0,210,0}, {0,255,0}, {100,255,0},
        {180,255,0}, {255,255,0}, {255,170,0}, {255,0,0}
    };

    // Inner width = 48.  All lines: indent + border + 48 content + border
    #define WA_W   48
    #define WA_IND "     "

    #define WF "\033[38;5;240m"
    #define WT "\033[38;5;208m"
    #define WG "\033[38;2;0;200;0m"
    #define WD "\033[38;5;242m"
    #define WB "\033[38;5;252m"
    #define WR "\033[0m"

    const int song_len = 213;  // 3:33
    int elapsed = 0;
    unsigned long last_sec = uptime_ms();

    getLock();
    Stream *out = getStream();
    out->print("\033[2J\033[?25l");
    releaseLock();

    for (;;) {
        // Advance clock
        if (uptime_ms() - last_sec >= 1000) {
            last_sec += 1000;
            elapsed++;
            if (elapsed >= song_len) elapsed = 0;
        }

        // Animate spectrum — drift with occasional spikes
        for (int i = 0; i < NBARS; i++) {
            bars[i] += (int)(esp_random() % 3) - 1;
            if (esp_random() % 8 == 0)
                bars[i] = 1 + (int)(esp_random() % 7);
            if (bars[i] < 0) bars[i] = 0;
            if (bars[i] > SROWS) bars[i] = SROWS;
        }

        int mm = elapsed / 60, ss = elapsed % 60;
        int seek = elapsed * 39 / (song_len > 0 ? song_len : 1); // 0-39

        getLock();
        out = getStream();
        out->print("\033[H\n\n\n");

        // --- Top border ---
        out->print(WF WA_IND "\xe2\x94\x8c");      // ┌
        wa_repeat(out, "\xe2\x94\x80", WA_W);       // ─ × 48
        out->print("\xe2\x94\x90\n");                // ┐

        // --- Title bar: 7 + 34 + 7 = 48 (□ is 2-wide) ---
        out->print(WF WA_IND "\xe2\x94\x82" WT      // │
            " WINAMP" WF
            "                                   "     // 34 spaces
            "- \xe2\x96\xa1 \xc3\x97 "               // - □ ×  (7 display chars)
            "\xe2\x94\x82\n");                       // │

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");       // ├
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");                // ┤

        // --- Time: "  ▶ XX:XX / 03:33" = 18 display (▶ is 2-wide), pad 30 ---
        out->printf(WF WA_IND "\xe2\x94\x82"
            "  " WG "\xe2\x96\xb6 %02d:%02d / 03:33" WF
            "                               "          // 30 spaces
            "\xe2\x94\x82\n", mm, ss);

        // --- Song: "  Rick Astley - Never Gonna Give You Up" = 39, pad 9 ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WG "Rick Astley - Never Gonna Give You Up" WF
            "         "                               // 9 spaces
            "\xe2\x94\x82\n");

        // --- Bitrate: "  128kbps  44kHz  stereo" = 24, pad 24 ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WD "128kbps  44kHz  stereo" WF
            "                        "               // 24 spaces
            "\xe2\x94\x82\n");

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");

        // --- 8-row spectrum (4 pad + 40 bars + 4 pad = 48) ---
        for (int row = SROWS - 1; row >= 0; row--) {
            out->printf(WF WA_IND "\xe2\x94\x82"
                "    \033[38;2;%d;%d;%dm",
                spc[row][0], spc[row][1], spc[row][2]);
            for (int i = 0; i < NBARS; i++)
                out->print(bars[i] > row ? "\xe2\x96\x88" : " ");
            out->print(WF "    \xe2\x94\x82\n");
        }

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");

        // --- Seek bar: "  " + 40 chars + "      " = 48 ---
        out->print(WF WA_IND "\xe2\x94\x82  " WD);
        for (int i = 0; i < 40; i++)
            out->print(i == seek
                ? (WG "\xe2\x97\x8f" WD)            // ● in green
                : "\xe2\x94\x80");                   // ─
        out->print(WF "      \xe2\x94\x82\n");   // 5 spaces (● is 2-wide)

        // --- Transport + volume slider ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WB
            "|\xe2\x97\x84  \xe2\x96\xb6  ||  "     // |◄  ▶  ||
            "\xe2\x96\xa0  \xe2\x96\xb6|" WF         //  ■  ▶|
            "   " WD "vol ");  // 3 spaces (◄/▶ are 2-wide)
        wa_repeat(out, "\xe2\x94\x80", 14);           // ─ × 14 (● is 2-wide)
        out->print(WG "\xe2\x97\x8f" WD);             // ● in green
        wa_repeat(out, "\xe2\x94\x80", 6);             // ─ × 4
        out->print(WF "  \xe2\x94\x82\n");

        // --- Bottom border ---
        out->print(WF WA_IND "\xe2\x94\x94");       // └
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\x98" WR "\n");          // ┘

        out->print("\n" WA_IND "Any key to exit\n");

        releaseLock();

        vTaskDelay(pdMS_TO_TICKS(67));  // ~15 fps

        if (getStream()->available()) {
            while (getStream()->available()) getStream()->read();
            break;
        }
    }

    setInteractive(false);
    getLock();
    getStream()->print("\033[?25h" WR "\n");
    releaseLock();

    #undef WA_W
    #undef WA_IND
    #undef WF
    #undef WT
    #undef WG
    #undef WD
    #undef WB
    #undef WR

    return 0;
}


int cmd_game(int argc, char **argv)
{
    if (!getAnsiEnabled()) {
        printfnl(SOURCE_COMMANDS, "Requires ANSI mode (color on)\n");
        return 1;
    }
    setInteractive(true);
    const int W = 30, H = 20;
    uint8_t grid[H][W], next[H][W], age[H][W];

    // Age palette: cyan-green → green → yellow-green → yellow → orange → red
    static const uint8_t pal[][3] = {
        {0,255,200}, {0,255,0}, {180,255,0},
        {255,220,0}, {255,128,0}, {255,0,0}
    };

    // Random initial state (~33% alive)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            grid[y][x] = (esp_random() % 3 == 0);
            age[y][x] = grid[y][x];
        }

    // Drain leftover input from command entry (e.g. trailing \n after \r)
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    getLock();
    Stream *out = getStream();
    out->print("\033[2J\033[?25l");   // clear screen + hide cursor
    releaseLock();

    for (int gen = 1; gen <= 500; gen++) {
        // Draw frame
        getLock();
        out = getStream();
        out->print("\033[H");         // cursor home

        for (int y = 0; y < H; y++) {
            int lc = -1;
            for (int x = 0; x < W; x++) {
                if (grid[y][x]) {
                    int a = age[y][x];
                    int c = (a<=1)?0 : (a<=3)?1 : (a<=6)?2 : (a<=10)?3 : (a<=16)?4 : 5;
                    if (c != lc) {
                        out->printf("\033[38;2;%d;%d;%dm", pal[c][0], pal[c][1], pal[c][2]);
                        lc = c;
                    }
                    out->print("\xe2\x96\x88\xe2\x96\x88");  // ██
                } else {
                    if (lc >= 0) { out->print("\033[0m"); lc = -1; }
                    out->print("  ");
                }
            }
            out->print("\033[0m\n");
        }
        out->printf("\033[0m Gen %-4d  Any key to exit", gen);
        releaseLock();

        vTaskDelay(pdMS_TO_TICKS(100));

        // Check for keypress to exit
        if (getStream()->available()) {
            while (getStream()->available()) getStream()->read();
            break;
        }

        // Compute next generation (toroidal wrap)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dy && !dx) continue;
                        n += grid[(y+dy+H)%H][(x+dx+W)%W];
                    }
                next[y][x] = grid[y][x] ? (n==2||n==3) : (n==3);
            }

        // Update grid and ages
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                age[y][x] = next[y][x]
                    ? (grid[y][x] ? (age[y][x] < 255 ? age[y][x]+1 : 255) : 1)
                    : 0;
                grid[y][x] = next[y][x];
            }
    }

    setInteractive(false);
    getLock();
    getStream()->print("\033[?25h\033[0m\n");  // show cursor + reset
    releaseLock();
    return 0;
}


int cmd_color(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "ANSI color: %s\n", getAnsiEnabled() ? "on" : "off");
        return 0;
    }
    if (!strcasecmp(argv[1], "on")) {
        setAnsiEnabled(true);
        printfnl(SOURCE_COMMANDS, "ANSI color enabled\n");
    } else if (!strcasecmp(argv[1], "off")) {
        setAnsiEnabled(false);
        printfnl(SOURCE_COMMANDS, "ANSI color disabled\n");
    } else {
        printfnl(SOURCE_COMMANDS, "Usage: color [on|off]\n");
        return 1;
    }
    return 0;
}


int cmd_clear( int argc, char **argv )
{
    if (!getAnsiEnabled()) {
        printfnl(SOURCE_COMMANDS, "Requires ANSI mode (color on)\n");
        return 1;
    }
    getLock();
    Stream *out = getStream();
    out->print("\033[2J\033[H");  // clear screen + cursor home
    releaseLock();
    return 0;
}


static int md5_file(const char *path)
{
    File f = LittleFS.open(path, "r");
    if (!f) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", path);
        return 1;
    }

    mbedtls_md5_context ctx;
    mbedtls_md5_init(&ctx);
    mbedtls_md5_starts_ret(&ctx);

    uint8_t buf[256];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        mbedtls_md5_update_ret(&ctx, buf, n);
    }
    f.close();

    uint8_t digest[16];
    mbedtls_md5_finish_ret(&ctx, digest);
    mbedtls_md5_free(&ctx);

    getLock();
    Stream *out = getStream();
    for (int i = 0; i < 16; i++)
        out->printf("%02x", digest[i]);
    out->printf("  %s\n", path);
    releaseLock();
    return 0;
}

int cmd_md5(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: md5 <file ...>\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[64];
        normalize_path(path, sizeof(path), argv[i]);
        if (has_glob_chars(path)) {
            char (*matches)[128];
            int count = glob_expand(path, &matches);
            if (!count) {
                printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[i]);
                rc = 1;
                continue;
            }
            for (int j = 0; j < count; j++)
                if (md5_file(matches[j])) rc = 1;
            free(matches);
        } else {
            if (md5_file(path)) rc = 1;
        }
    }
    return rc;
}

static int sha256_file(const char *path)
{
    File f = LittleFS.open(path, "r");
    if (!f) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", path);
        return 1;
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);

    uint8_t buf[256];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0) break;
        mbedtls_sha256_update_ret(&ctx, buf, n);
    }
    f.close();

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&ctx, digest);
    mbedtls_sha256_free(&ctx);

    getLock();
    Stream *out = getStream();
    for (int i = 0; i < 32; i++)
        out->printf("%02x", digest[i]);
    out->printf("  %s\n", path);
    releaseLock();
    return 0;
}

int cmd_sha256(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: sha256 <file ...>\n");
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[64];
        normalize_path(path, sizeof(path), argv[i]);
        if (has_glob_chars(path)) {
            char (*matches)[128];
            int count = glob_expand(path, &matches);
            if (!count) {
                printfnl(SOURCE_COMMANDS, "No match: %s\n", argv[i]);
                rc = 1;
                continue;
            }
            for (int j = 0; j < count; j++)
                if (sha256_file(matches[j])) rc = 1;
            free(matches);
        } else {
            if (sha256_file(path)) rc = 1;
        }
    }
    return rc;
}


int cmd_help( int argc, char **argv )
{
    printfnl( SOURCE_COMMANDS, "Available commands:\n" );
    printfnl( SOURCE_COMMANDS, "  art                                Is it art? (ANSI)\n" );
    printfnl( SOURCE_COMMANDS, "  cat|list {file}                    Show file contents\n" );
    printfnl( SOURCE_COMMANDS, "  clear|cls                          Clear screen (ANSI)\n" );
    printfnl( SOURCE_COMMANDS, "  color [on|off]                     Show/toggle ANSI color\n" );
#if defined(INCLUDE_BASIC_COMPILER) || defined(INCLUDE_C_COMPILER)
    printfnl( SOURCE_COMMANDS, "  compile {file} [run]               Compile .bas/.c to .wasm\n" );
#endif
    printfnl( SOURCE_COMMANDS, "  config [set|unset|reset]           Show or change settings\n" );
    printfnl( SOURCE_COMMANDS, "  copy|cp {src} {dst}                Copy file\n" );
    printfnl( SOURCE_COMMANDS, "  cue [load|start|stop|status]       Cue timeline engine\n" );
    printfnl( SOURCE_COMMANDS, "  debug [off|{source} [on|off]]      Show/set debug sources\n" );
    printfnl( SOURCE_COMMANDS, "  deflate|gzip {file} [out] [level]  Compress to gzip\n" );
    printfnl( SOURCE_COMMANDS, "  del|delete|rm {file}               Delete file\n" );
    printfnl( SOURCE_COMMANDS, "  df                                 Show filesystem usage\n" );
    printfnl( SOURCE_COMMANDS, "  dir|ls [path]                      List files\n" );
    printfnl( SOURCE_COMMANDS, "  edit {file}                        Edit file (nano-like)\n" );
    printfnl( SOURCE_COMMANDS, "  game                               Waste time (ANSI)\n" );
    printfnl( SOURCE_COMMANDS, "  gpio [set|out|in|read]             Show/configure GPIO pins\n" );
    printfnl( SOURCE_COMMANDS, "  gps [info|set|save|restart|send]   GPS status or configure\n" );
    printfnl( SOURCE_COMMANDS, "  grep {pattern} [file]              Search file contents\n" );
    printfnl( SOURCE_COMMANDS, "  help|?                             Show this help\n" );
    printfnl( SOURCE_COMMANDS, "  hexdump {file} [count]             Hex dump (default 256)\n" );
    printfnl( SOURCE_COMMANDS, "  history                            Show command history\n" );
    printfnl( SOURCE_COMMANDS, "  inflate|gunzip {file} [output]     Decompress gzip/zlib\n" );
    printfnl( SOURCE_COMMANDS, "  led [set|clear|count]              Show/set LED config\n" );
    printfnl( SOURCE_COMMANDS, "  load {file}                        Receive file via serial\n" );
    printfnl( SOURCE_COMMANDS, "  log [to|save|close|stop]           Debug log buffer/file\n" );
    printfnl( SOURCE_COMMANDS, "  lora|radio [freq|power|bw|sf|...]  LoRa status or configure\n" );
    printfnl( SOURCE_COMMANDS, "  md5|md5sum {file}                  Compute MD5 hash\n" );
    printfnl( SOURCE_COMMANDS, "  mem|free                           Show heap memory stats\n" );
    printfnl( SOURCE_COMMANDS, "  mkdir {dir}                        Create directory\n" );
    printfnl( SOURCE_COMMANDS, "  mqtt [enable|disable|connect|...]  MQTT status or control\n" );
    printfnl( SOURCE_COMMANDS, "  move|mv|ren {old} {new}            Rename/move file\n" );
    printfnl( SOURCE_COMMANDS, "  param {id} {value}                 Set script parameter\n" );
    printfnl( SOURCE_COMMANDS, "  ps                                 Show tasks and stack usage\n" );
    printfnl( SOURCE_COMMANDS, "  psram [test|freq|cache]            PSRAM status/diagnostics\n" );
    printfnl( SOURCE_COMMANDS, "  reboot                             Reboot the system\n" );
    printfnl( SOURCE_COMMANDS, "  rmdir {dir}                        Remove empty directory\n" );
    printfnl( SOURCE_COMMANDS, "  run {file}                         Run script (.bas/.wasm)\n" );
    printfnl( SOURCE_COMMANDS, "  sensors                            Show sensor readings\n" );
    printfnl( SOURCE_COMMANDS, "  sha256|sha256sum {file}            Compute SHA-256 hash\n" );
    printfnl( SOURCE_COMMANDS, "  status                             System overview\n" );
    printfnl( SOURCE_COMMANDS, "  stop                               Stop running script\n" );
    printfnl( SOURCE_COMMANDS, "  tc                                 Show thread count\n" );
    printfnl( SOURCE_COMMANDS, "  time|date                          Show current date/time\n" );
    printfnl( SOURCE_COMMANDS, "  uptime                             Show system uptime\n" );
    printfnl( SOURCE_COMMANDS, "  version|ver                        Show firmware version\n" );
#ifdef INCLUDE_WASM
    printfnl( SOURCE_COMMANDS, "  wasm [status|info <file>]          WASM runtime status/info\n" );
#endif
    printfnl( SOURCE_COMMANDS, "  wifi [enable|disable|ssid|pass]    WiFi status or control\n" );
    printfnl( SOURCE_COMMANDS, "  winamp                             Audio visualizer (ANSI)\n" );
    return 0;
}


#ifdef INCLUDE_WASM
int cmd_wasm(int argc, char **argv)
{
    if (argc < 2 || !strcasecmp(argv[1], "status")) {
        printfnl(SOURCE_COMMANDS, "WASM Runtime:\n");
        printfnl(SOURCE_COMMANDS, "  Running: %s\n", wasm_is_running() ? "yes" : "no");
        if (wasm_is_running()) {
            const char *p = wasm_get_current_path();
            printfnl(SOURCE_COMMANDS, "  Module:  %s\n", (p && p[0]) ? p : "(unknown)");
        }
        return 0;
    }

    if (!strcasecmp(argv[1], "info")) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, "Usage: wasm info <file.wasm>\n");
            return 1;
        }
        char path[64];
        normalize_path(path, sizeof(path), argv[2]);
        File f = LittleFS.open(path, "r");
        if (!f) {
            printfnl(SOURCE_COMMANDS, "Cannot open %s\n", path);
            return 1;
        }
        printfnl(SOURCE_COMMANDS, "WASM Module: %s\n", path);
        printfnl(SOURCE_COMMANDS, "  Size: %u bytes\n", (unsigned)f.size());
        f.close();
        return 0;
    }

    printfnl(SOURCE_COMMANDS, "Usage: wasm [status | info <file>]\n");
    return 1;
}
#endif

int cmd_psram(int argc, char **argv)
{
    if (argc >= 2 && !strcasecmp(argv[1], "test")) {
        bool forever = (argc >= 3 && !strcasecmp(argv[2], "forever"));
        log_free();
        shell.historyFree();
        int result = psram_test(forever);
        shell.historyInit();
        log_init();
        return result;
    }
    if (argc >= 2 && !strcasecmp(argv[1], "cache")) {
        psram_print_cache_detail();
        return 0;
    }
    if (argc >= 3 && !strcasecmp(argv[1], "freq")) {
        uint32_t mhz = strtol(argv[2], NULL, 10);
        if (mhz < 5 || mhz > 80) {
            printfnl(SOURCE_COMMANDS, "Usage: psram freq <5-80>  (MHz)\n");
            return 1;
        }
        if (psram_change_freq(mhz * 1000000) < 0) {
            printfnl(SOURCE_COMMANDS, "Failed to change PSRAM frequency\n");
            return 1;
        }
        uint32_t actual = psram_get_freq();
        if (actual != mhz * 1000000)
            printfnl(SOURCE_COMMANDS, "PSRAM SPI clock: requested %u MHz, actual %.2f MHz\n",
                     mhz, actual / 1000000.0f);
        else
            printfnl(SOURCE_COMMANDS, "PSRAM SPI clock set to %u MHz\n", mhz);
        return 0;
    }
    // Default: show status
    printfnl(SOURCE_COMMANDS, "PSRAM:\n");
    printfnl(SOURCE_COMMANDS, "  Available:   %s\n", psram_available() ? "yes" : "no");
    if (psram_get_freq())
        printfnl(SOURCE_COMMANDS, "  SPI clock:   %.2f MHz\n", psram_get_freq() / 1000000.0f);
    printfnl(SOURCE_COMMANDS, "  Size:        %u bytes (%u KB)\n", psram_size(), psram_size()/1024);
    printfnl(SOURCE_COMMANDS, "  Used:        %u bytes\n", psram_bytes_used());
    printfnl(SOURCE_COMMANDS, "  Free:        %u bytes\n", psram_bytes_free());
    printfnl(SOURCE_COMMANDS, "  Contiguous:  %u bytes\n", psram_bytes_contiguous());
    printfnl(SOURCE_COMMANDS, "  Alloc slots: %d / %d\n", psram_alloc_count(), psram_alloc_entries_max());
    psram_print_map();
    psram_print_cache_map();
#if PSRAM_CACHE_PAGES > 0
    uint32_t hits = psram_cache_hits(), misses = psram_cache_misses();
    uint32_t total = hits + misses;
    printfnl(SOURCE_COMMANDS, "  Cache:       %d x %d bytes (%u KB DRAM)\n",
             PSRAM_CACHE_PAGES, PSRAM_CACHE_PAGE_SIZE,
             (PSRAM_CACHE_PAGES * PSRAM_CACHE_PAGE_SIZE) / 1024);
    printfnl(SOURCE_COMMANDS, "  Cache hits:  %u / %u (%u%%)\n",
             hits, total, total ? (hits * 100 / total) : 0);
#endif
    return 0;
}


#if defined(INCLUDE_BASIC_COMPILER) || defined(INCLUDE_C_COMPILER)

#ifdef INCLUDE_BASIC_COMPILER
static void bw_diag_cb(const char *msg, void *) {
    printfnl(SOURCE_COMMANDS, "%s", msg);
}
#endif

#ifdef INCLUDE_C_COMPILER
static void cw_diag_cb(const char *msg, void *) {
    printfnl(SOURCE_COMMANDS, "%s", msg);
}
#endif

int cmd_compile(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, "Usage: compile <file.bas|file.c> [run]\n");
        return 1;
    }

    char path[64];
    normalize_path(path, sizeof(path), argv[1]);

    // Check extension
    const char *dot = strrchr(path, '.');
    int is_bas = 0, is_c = 0;
#ifdef INCLUDE_BASIC_COMPILER
    if (dot && strcmp(dot, ".bas") == 0) is_bas = 1;
#endif
#ifdef INCLUDE_C_COMPILER
    if (dot && strcmp(dot, ".c") == 0) is_c = 1;
#endif
    if (!is_bas && !is_c) {
        printfnl(SOURCE_COMMANDS, "Unsupported file type (use .bas or .c)\n");
        return 1;
    }

    // Read source file
    File f = LittleFS.open(path, "r");
    if (!f) {
        printfnl(SOURCE_COMMANDS, "Cannot open %s\n", path);
        return 1;
    }
    int slen = f.size();
    char *src = (char *)malloc(slen + 1);
    if (!src) {
        f.close();
        printfnl(SOURCE_COMMANDS, "Out of memory\n");
        return 1;
    }
    f.readBytes(src, slen);
    src[slen] = 0;
    f.close();

    // Compile
    // Use bw_ or cw_ prefixed Buf type depending on compiler
    // Both are identical structs: { uint8_t *data; int len, cap; }
    uint8_t *wasm_data = NULL;
    int wasm_len = 0;

#ifdef INCLUDE_BASIC_COMPILER
    if (is_bas) {
        bw_on_error = bw_diag_cb;
        bw_on_info = bw_diag_cb;
        bw_cb_ctx = NULL;

        bw_Buf result;
        bw_buf_init(&result);
        if (setjmp(bw_bail) == 0) {
            result = bas2wasm_compile_buffer(src, slen);
        }
        free(src);

        if (result.len == 0) {
            printfnl(SOURCE_COMMANDS, "Compilation failed\n");
            bas2wasm_reset();
            return 1;
        }
        wasm_data = result.data;
        wasm_len = result.len;
        // Don't free result yet — data pointer used below
        bas2wasm_reset();
    }
#endif

#ifdef INCLUDE_C_COMPILER
    if (is_c) {
        cw_on_error = cw_diag_cb;
        cw_on_info = cw_diag_cb;
        cw_cb_ctx = NULL;

        cw_Buf result;
        cw_buf_init(&result);
        if (setjmp(cw_bail) == 0) {
            result = c2wasm_compile_buffer(src, slen, path);
        }
        free(src);

        if (result.len == 0) {
            printfnl(SOURCE_COMMANDS, "Compilation failed\n");
            c2wasm_reset();
            return 1;
        }
        wasm_data = result.data;
        wasm_len = result.len;
        c2wasm_reset();
    }
#endif

    // Write .wasm output
    char out_path[64];
    snprintf(out_path, sizeof(out_path), "%.*s.wasm", (int)(dot - path), path);

    File out = LittleFS.open(out_path, FILE_WRITE);
    if (!out) {
        printfnl(SOURCE_COMMANDS, "Cannot create %s\n", out_path);
        free(wasm_data);
        return 1;
    }
    out.write(wasm_data, wasm_len);
    out.close();
    printfnl(SOURCE_COMMANDS, "Wrote %d bytes to %s\n", wasm_len, out_path);
    free(wasm_data);

    // Optionally auto-run
    if (argc >= 3 && strcmp(argv[2], "run") == 0) {
        set_script_program(out_path);
    }

    return 0;
}
#endif /* INCLUDE_BASIC_COMPILER || INCLUDE_C_COMPILER */

// Subcommand lists for tab completion (NULL-terminated, stored in .rodata)
static const char * const subs_color[]  = { "on", "off", NULL };
static const char * const subs_config[] = { "set", "unset", "reset", NULL };
static const char * const subs_cue[]    = { "load", "start", "stop", "status", NULL };
static const char * const subs_debug[]  = {
    "off", "system", "basic", "wasm", "commands", "shell",
    "gps", "gps_raw", "lora", "lora_raw", "wifi", "fsync",
    "sensors", "mqtt", "other", NULL
};
static const char * const subs_onoff[]  = { "on", "off", NULL };

// TabCompleteFunc callbacks for multi-level completion
static const char * const * tc_debug(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_debug;
    if (wordIndex == 2 && nWords >= 2) {
        // "debug off" takes no further args
        if (strcasecmp(words[1], "off") == 0) return NULL;
        return subs_onoff;
    }
    return NULL;
}

static const char * const * tc_config(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_config;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "set") == 0 || strcasecmp(words[1], "unset") == 0) {
            // If partial word contains '.', show full section.key list
            if (nWords > 2 && strchr(words[2], '.'))
                return config_get_key_list();
            // Otherwise show section names (with trailing dot)
            return config_get_section_list();
        }
    }
    if (wordIndex == 3 && nWords >= 3) {
        if (strcasecmp(words[1], "set") == 0) {
            int t = config_get_key_type(words[2]);
            if (t == 4) return subs_onoff;           // CFG_BOOL → on/off
            if (t == 0) return TAB_COMPLETE_VALUE_STR;   // CFG_STR
            if (t == 1) return TAB_COMPLETE_VALUE_FLOAT; // CFG_FLOAT
            if (t == 2) return TAB_COMPLETE_VALUE_INT;   // CFG_INT
            if (t == 3) return TAB_COMPLETE_VALUE_HEX;   // CFG_HEX
            return TAB_COMPLETE_VALUE;
        }
    }
    return NULL;
}

static const char * const * tc_cue(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_cue;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "load") == 0) return TAB_COMPLETE_FILES;
        if (strcasecmp(words[1], "start") == 0) return TAB_COMPLETE_VALUE_INT;
    }
    return NULL;
}
static const char * const subs_gpio[]   = { "set", "out", "in", "read", NULL };
static const char * const subs_gpio_pull[] = { "up", "down", "none", NULL };
static const char * const subs_gps[]    = { "info", "set", "save", "restart", "send", NULL };
static const char * const subs_gps_set[] = { "baud", "rate", "mode", "nmea", NULL };
static const char * const subs_gps_restart[] = { "hot", "warm", "cold", "factory", NULL };
static const char * const subs_gps_mode[] = { "gps", "bds", "glonass", "gps+bds",
                                              "gps+glonass", "bds+glonass", "all", NULL };
static const char * const subs_led[]    = { "set", "clear", "count", NULL };
static const char * const subs_lora[]   = { "freq", "power", "bw", "sf", "cr", "mode",
                                            "save", "restart", "send", NULL };
static const char * const subs_lora_mode[] = { "lora", "fsk", NULL };
static const char * const subs_log[]    = { "to", "save", "close", "stop", NULL };

static const char * const * tc_log(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_log;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "to") == 0)   return TAB_COMPLETE_FILES;
        if (strcasecmp(words[1], "save") == 0) return TAB_COMPLETE_FILES;
    }
    return NULL;
}

static const char * const subs_mqtt[]   = { "broker", "port", "enable", "disable",
                                            "connect", "disconnect", "pub", NULL };
static const char * const subs_psram[]  = { "test", "freq", "cache", NULL };
static const char * const subs_psram_test[] = { "forever", NULL };
static const char * const subs_wasm[]   = { "status", "info", NULL };
static const char * const subs_wifi[]   = { "enable", "disable", "ssid", "password", NULL };

static const char * const * tc_wifi(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_wifi;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "ssid") == 0 || strcasecmp(words[1], "password") == 0)
            return TAB_COMPLETE_VALUE_STR;
    }
    return NULL;
}

static const char * const * tc_gpio(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_gpio;
    if (wordIndex == 2 && nWords >= 2) {
        // set/out/in/read all take a pin number
        if (strcasecmp(words[1], "set") == 0 || strcasecmp(words[1], "out") == 0 ||
            strcasecmp(words[1], "in") == 0  || strcasecmp(words[1], "read") == 0)
            return TAB_COMPLETE_VALUE_INT;
    }
    if (wordIndex == 3 && nWords >= 3) {
        if (strcasecmp(words[1], "set") == 0 || strcasecmp(words[1], "out") == 0)
            return TAB_COMPLETE_VALUE_INT;  // 0 or 1
        if (strcasecmp(words[1], "in") == 0)
            return subs_gpio_pull;          // up/down/none
    }
    return NULL;
}

static const char * const * tc_lora(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_lora;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "freq") == 0)  return TAB_COMPLETE_VALUE_FLOAT;
        if (strcasecmp(words[1], "power") == 0) return TAB_COMPLETE_VALUE_INT;
        if (strcasecmp(words[1], "bw") == 0)    return TAB_COMPLETE_VALUE_FLOAT;
        if (strcasecmp(words[1], "sf") == 0)    return TAB_COMPLETE_VALUE_INT;
        if (strcasecmp(words[1], "cr") == 0)    return TAB_COMPLETE_VALUE_INT;
        if (strcasecmp(words[1], "mode") == 0)  return subs_lora_mode;
        if (strcasecmp(words[1], "send") == 0)  return TAB_COMPLETE_VALUE_STR;
    }
    return NULL;
}

static const char * const * tc_mqtt(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_mqtt;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "broker") == 0) return TAB_COMPLETE_VALUE_STR;
        if (strcasecmp(words[1], "port") == 0)   return TAB_COMPLETE_VALUE_INT;
        if (strcasecmp(words[1], "pub") == 0)    return TAB_COMPLETE_VALUE_STR;
    }
    if (wordIndex == 3 && nWords >= 3) {
        if (strcasecmp(words[1], "pub") == 0) return TAB_COMPLETE_VALUE_STR;
    }
    return NULL;
}

static const char * const * tc_gps(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_gps;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "set") == 0)     return subs_gps_set;
        if (strcasecmp(words[1], "restart") == 0)  return subs_gps_restart;
        if (strcasecmp(words[1], "send") == 0)     return TAB_COMPLETE_VALUE_STR;
    }
    if (wordIndex == 3 && nWords >= 3) {
        if (strcasecmp(words[1], "set") == 0) {
            if (strcasecmp(words[2], "baud") == 0) return TAB_COMPLETE_VALUE_INT;
            if (strcasecmp(words[2], "rate") == 0) return TAB_COMPLETE_VALUE_INT;
            if (strcasecmp(words[2], "mode") == 0) return subs_gps_mode;
            if (strcasecmp(words[2], "nmea") == 0) return TAB_COMPLETE_VALUE_STR;
        }
    }
    return NULL;
}

static const char * const * tc_led(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_led;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "set") == 0)   return TAB_COMPLETE_VALUE_INT;  // channel
        if (strcasecmp(words[1], "count") == 0) return TAB_COMPLETE_VALUE_INT;  // channel
    }
    if (wordIndex == 3 && nWords >= 3) {
        if (strcasecmp(words[1], "set") == 0)   return TAB_COMPLETE_VALUE;      // index/range/all
        if (strcasecmp(words[1], "count") == 0) return TAB_COMPLETE_VALUE_INT;  // count
    }
    if (wordIndex == 4 && nWords >= 4) {
        if (strcasecmp(words[1], "set") == 0)   return TAB_COMPLETE_VALUE_HEX;  // #RRGGBB
    }
    return NULL;
}

static const char * const * tc_psram(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_psram;
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "test") == 0) return subs_psram_test;
        if (strcasecmp(words[1], "freq") == 0) return TAB_COMPLETE_VALUE_INT;
    }
    return NULL;
}

static const char * const * tc_param(int wordIndex, const char **words, int nWords) {
    (void)words; (void)nWords;
    if (wordIndex == 1) return TAB_COMPLETE_VALUE_INT;  // index
    if (wordIndex == 2) return TAB_COMPLETE_VALUE_INT;  // value
    return NULL;
}

void init_commands(Stream *dev)
{
    shell.attach(*dev);
    shell.historyInit();

    //Test Commands
    shell.addCommand("test", test);

    // Commands — fileArgs=true for filename completion, subcommands for subcommand completion
    shell.addCommand("?", cmd_help);
    shell.addCommand("art", cmd_art);
    shell.addCommand("cat", listFile, "*");
    shell.addCommand("clear", cmd_clear);
    shell.addCommand("cls", cmd_clear);
    shell.addCommand("color", cmd_color, NULL, subs_color);
#if defined(INCLUDE_BASIC_COMPILER) || defined(INCLUDE_C_COMPILER)
    shell.addCommand("compile", cmd_compile, "*.bas;*.c");
#endif
    shell.addCommand("config", cmd_config, NULL, NULL, tc_config);
    shell.addCommand("copy", cmd_cp, "*");
    shell.addCommand("cp", cmd_cp, "*");
    shell.addCommand("cue", cmd_cue, NULL, NULL, tc_cue);
    shell.addCommand("debug", cmd_debug, NULL, NULL, tc_debug);
    shell.addCommand("del", delFile, "*");
    shell.addCommand("delete", delFile, "*");
    shell.addCommand("df", cmd_df);
    shell.addCommand("deflate", cmd_deflate, "*");
    shell.addCommand("dir", listDir, "/");
    shell.addCommand("edit", cmd_edit, "*");
    shell.addCommand("free", cmd_mem);
    shell.addCommand("game", cmd_game);
    shell.addCommand("gpio", cmd_gpio, NULL, NULL, tc_gpio);
    shell.addCommand("gps", cmd_gps, NULL, NULL, tc_gps);
    shell.addCommand("grep", cmd_grep, "*");
    shell.addCommand("gunzip", cmd_inflate, "*.gz");
    shell.addCommand("gzip", cmd_deflate, "*");
    shell.addCommand("help", cmd_help);
    shell.addCommand("hexdump", cmd_hexdump, "*");
    shell.addCommand("inflate", cmd_inflate, "*.gz");
    shell.addCommand("led", cmd_led, NULL, NULL, tc_led);
    shell.addCommand("list", listFile, "*");
    shell.addCommand("load", loadFile, "*.bas;*.c;*.wasm");
    shell.addCommand("log", cmd_log, NULL, NULL, tc_log);
    shell.addCommand("lora", cmd_lora, NULL, NULL, tc_lora);
    shell.addCommand("ls", listDir, "/");
    shell.addCommand("md5", cmd_md5, "*");
    shell.addCommand("md5sum", cmd_md5, "*");
    shell.addCommand("mem", cmd_mem);
    shell.addCommand("mkdir", cmd_mkdir, "/");
    shell.addCommand("move", renFile, "*");
    shell.addCommand("mqtt", cmd_mqtt, NULL, NULL, tc_mqtt);
    shell.addCommand("mv", renFile, "*");
    shell.addCommand("param", paramBasic, NULL, NULL, tc_param);
    shell.addCommand("ps", cmd_ps);
    shell.addCommand("psram", cmd_psram, NULL, NULL, tc_psram);
    shell.addCommand("radio", cmd_lora, NULL, NULL, tc_lora);
    shell.addCommand("reboot", cmd_reboot);
    shell.addCommand("ren", renFile, "*");
    shell.addCommand("rm", delFile, "*");
    shell.addCommand("rmdir", cmd_rmdir, "/");
    shell.addCommand("run", runBasic, "*.bas;*.c;*.wasm");
    shell.addCommand("sensors", cmd_sensors);
    shell.addCommand("sha256", cmd_sha256, "*");
    shell.addCommand("sha256sum", cmd_sha256, "*");
    shell.addCommand("status", cmd_status);
    shell.addCommand("stop", stopBasic);
    shell.addCommand("tc", tc);
    shell.addCommand("time", cmd_time);
    shell.addCommand("date", cmd_time);
    shell.addCommand("uptime", cmd_time);
    shell.addCommand("ver", cmd_version);
    shell.addCommand("version", cmd_version);
    shell.addCommand("wifi", cmd_wifi, NULL, NULL, tc_wifi);
    shell.addCommand("winamp", cmd_winamp);
#ifdef INCLUDE_WASM
    shell.addCommand("wasm", cmd_wasm, "*.wasm", subs_wasm);
#endif
}

void run_commands(void)
{
    shell.executeIfInput();
}

void setCLIEcho(bool echo)
{
  shell.setEcho(echo);
}