#ifndef main_h
#define main_h

#include "board.h"
#include "led.h"
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "driver/i2c_master.h"

// Monotonic millisecond timer (replaces Arduino millis())
// Wraps at ~49 days, same as Arduino. ISR-safe on ESP32.
static inline uint32_t uptime_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

// Monotonic microsecond timer (replaces Arduino micros())
// Wraps at ~71 minutes, same as Arduino. ISR-safe on ESP32.
static inline uint32_t uptime_us(void) {
    return (uint32_t)esp_timer_get_time();
}

// LittleFS VFS mount point — POSIX calls need the full path.
#define LFS_PREFIX "/littlefs"

// Prepend the LittleFS mount point to an application-level path.
// Application paths start with '/' (e.g. "/config.ini").
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static inline const char *lfs_path(char *buf, size_t bufsz, const char *path)
{
    snprintf(buf, bufsz, LFS_PREFIX "%s", path);
    return buf;
}
#pragma GCC diagnostic pop

// Get file size via seek (for FILE* handles).
static inline long fsize(FILE *f)
{
    long pos = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, pos, SEEK_SET);
    return sz;
}

// Check file existence via POSIX stat() instead of LittleFS.exists(),
// which internally calls open() and triggers VFS error logs for missing files.
static inline bool file_exists(const char *path)
{
    char fullpath[256];
    lfs_path(fullpath, sizeof(fullpath), path);
    struct stat st;
    return (stat(fullpath, &st) == 0);
}

// Normalize a LittleFS path: prepend '/' if missing.
// Writes result into dst (must be at least dstsz bytes).
static inline void normalize_path(char *dst, size_t dstsz, const char *src)
{
    if (src[0] != '/')
        snprintf(dst, dstsz, "/%s", src);
    else {
        strncpy(dst, src, dstsz);
        dst[dstsz - 1] = '\0';
    }
}

//#define configGENERATE_RUN_TIME_STATS
//#define configUSE_STATS_FORMATTING_FUNCTIONS

// Pin definitions are in board.h

// Structure to hold result
typedef struct {
    float distance;   // Distance in meters
    float bearing_deg;  // Bearing in degrees
} GeoResult;

GeoResult xy_to_polar(float x1, float y1, float x2, float y2);
void latlon_to_meters(float latitude_deg, float longitude_deg,
                      float *x_offset_meters, float *y_offset_meters);

extern bool littlefs_mounted;
extern i2c_master_bus_handle_t i2c_bus;

#endif