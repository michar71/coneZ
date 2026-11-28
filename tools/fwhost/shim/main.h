// Host shim for firmware/src/main.h -- see ../README.txt
#ifndef FWHOST_MAIN_H
#define FWHOST_MAIN_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>

// The device mounts LittleFS at /littlefs; here the "filesystem" is a sandbox
// directory chosen on the command line.
extern char fwhost_fs_root[512];

static inline const char *lfs_path(char *buf, size_t bufsz, const char *path)
{
    snprintf(buf, bufsz, "%s%s", fwhost_fs_root, path);
    return buf;
}

static inline long fsize(FILE *f)
{
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, pos, SEEK_SET);
    return sz;
}

static inline bool file_exists(const char *path)
{
    char fullpath[768];
    lfs_path(fullpath, sizeof(fullpath), path);
    struct stat st;
    return (stat(fullpath, &st) == 0);
}

// Deterministic clock: advances only when the guest sleeps (see fwhost.cpp).
unsigned long uptime_ms(void);
static inline void inc_thread_count(int) {}

#endif
