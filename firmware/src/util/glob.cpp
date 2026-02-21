#include "glob.h"
#include "main.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

bool has_glob_chars(const char *s)
{
    for (; *s; s++)
        if (*s == '*' || *s == '?') return true;
    return false;
}

bool glob_match(const char *pat, const char *str)
{
    while (*pat) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return true;  // trailing * matches everything
            for (; *str; str++)
                if (glob_match(pat, str)) return true;
            return glob_match(pat, str);  // try matching empty remainder
        } else if (*pat == '?' && *str) {
            pat++;
            str++;
        } else if (*pat == *str) {
            pat++;
            str++;
        } else {
            return false;
        }
    }
    return *str == '\0';
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
int glob_expand(const char *pattern, char (**results)[128])
{
    *results = NULL;

    // Split into directory and file pattern
    char dir[64];
    const char *filepart;
    const char *lastSlash = strrchr(pattern, '/');
    if (lastSlash) {
        int dirLen = lastSlash - pattern;
        if (dirLen == 0) dirLen = 1;  // root "/"
        if (dirLen >= (int)sizeof(dir)) dirLen = sizeof(dir) - 1;
        memcpy(dir, pattern, dirLen);
        dir[dirLen] = '\0';
        filepart = lastSlash + 1;
    } else {
        strcpy(dir, "/");
        filepart = pattern;
    }

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), dir);
    DIR *d = opendir(fpath);
    if (!d) return 0;

    char (*buf)[128] = (char (*)[128])malloc(MAX_GLOB_MATCHES * 128);
    if (!buf) { closedir(d); return 0; }

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < MAX_GLOB_MATCHES) {
        // Skip directories — only match files
        // Use stat to check if entry is a directory
        char entpath[192];
        snprintf(entpath, sizeof(entpath), "%s/%s", fpath, ent->d_name);
        struct stat st;
        if (stat(entpath, &st) == 0 && S_ISDIR(st.st_mode))
            continue;

        if (glob_match(filepart, ent->d_name)) {
            snprintf(buf[count], 128, "%s%s%s",
                     dir, (dir[strlen(dir) - 1] == '/') ? "" : "/",
                     ent->d_name);
            count++;
        }
    }
    closedir(d);

    if (count == 0) {
        free(buf);
        return 0;
    }

    // Sort results alphabetically
    qsort(buf, count, 128, [](const void *a, const void *b) -> int {
        return strcmp((const char *)a, (const char *)b);
    });

    *results = buf;
    return count;
}
#pragma GCC diagnostic pop
