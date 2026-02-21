#include <Arduino.h>
#include <LittleFS.h>
#include "glob.h"

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

int glob_expand(const char *pattern, char (**results)[64])
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

    File root = LittleFS.open(dir);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 0;
    }

    char (*buf)[64] = (char (*)[64])malloc(MAX_GLOB_MATCHES * 64);
    if (!buf) { root.close(); return 0; }

    int count = 0;
    File f = root.openNextFile();
    while (f && count < MAX_GLOB_MATCHES) {
        if (!f.isDirectory() && glob_match(filepart, f.name())) {
            snprintf(buf[count], 64, "%s%s%s",
                     dir, (dir[strlen(dir) - 1] == '/') ? "" : "/",
                     f.name());
            count++;
        }
        f = root.openNextFile();
    }
    root.close();

    if (count == 0) {
        free(buf);
        return 0;
    }

    // Sort results alphabetically
    qsort(buf, count, 64, [](const void *a, const void *b) -> int {
        return strcmp((const char *)a, (const char *)b);
    });

    *results = buf;
    return count;
}
