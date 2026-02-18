/**
 * ConeZ WASM API
 *
 * Include this header when compiling C/C++ modules targeting the ConeZ
 * wasm3 runtime. All functions are imported from the "env" module.
 *
 * Build C modules with:
 *   clang --target=wasm32 -O2 -nostdlib \
 *     -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop \
 *     -Wl,--allow-undefined \
 *     -o module.wasm module.c
 *
 * Entry points (export one or both):
 *   void setup()  — called once at load
 *   void loop()   — called repeatedly until stopped
 *
 * Or export a single entry:
 *   void _start()  or  int main()
 *
 * WASI support: The runtime implements wasi_snapshot_preview1 fd_write
 * (stdout/stderr), fd_seek, fd_close, and proc_exit. This means standard
 * printf() works when targeting wasm32-wasi or wasm32-wasip1 (e.g. Rust
 * println!()). See examples/rust_rainbow/ for a Rust example.
 *
 * All WASM output goes through the SOURCE_WASM debug channel, filterable
 * independently from BASIC via "debug WASM on/off" or config debug.wasm.
 */

#ifndef CONEZ_API_H
#define CONEZ_API_H

/* API version: 0xMMmmpp (major, minor, patch) */
#define CONEZ_API_VERSION 0

#include <stdint.h>

#ifndef __cplusplus
#ifndef bool
#define bool    int
#define true    1
#define false   0
#endif
#endif

/* ---- LED ---- */

/* Set a single pixel on a channel (1-4) to an RGB color. */
__attribute__((import_module("env"), import_name("led_set_pixel")))
void led_set_pixel(int channel, int pos, int r, int g, int b);

/* Fill an entire channel (1-4) with a solid RGB color. */
__attribute__((import_module("env"), import_name("led_fill")))
void led_fill(int channel, int r, int g, int b);

/* Push LED buffer to the strip. Call after writing pixels. */
__attribute__((import_module("env"), import_name("led_show")))
void led_show(void);

/* Return the number of LEDs on a channel (1-4). */
__attribute__((import_module("env"), import_name("led_count")))
int led_count(int channel);

/* Apply gamma correction to a 0-255 value. */
__attribute__((import_module("env"), import_name("led_gamma8")))
int led_gamma8(int val);

/* Enable (1) or disable (0) automatic gamma correction on all LED output. */
__attribute__((import_module("env"), import_name("led_set_gamma")))
void led_set_gamma(int enable);

/*
 * Write count*3 bytes of interleaved R,G,B data from buf to a channel.
 * Much faster than per-pixel led_set_pixel() calls for full-strip updates.
 * Does NOT call led_show() — call it yourself after filling all channels.
 */
__attribute__((import_module("env"), import_name("led_set_buffer")))
void led_set_buffer(int channel, const void *rgb_data, int count);

/*
 * Shift pixels on a channel. Positive amount shifts right (new pixels at start),
 * negative shifts left (new pixels at end). New pixels are filled with (r,g,b).
 */
__attribute__((import_module("env"), import_name("led_shift")))
void led_shift(int channel, int amount, int r, int g, int b);

/* Rotate pixels on a channel. Positive rotates right, negative left. */
__attribute__((import_module("env"), import_name("led_rotate")))
void led_rotate(int channel, int amount);

/* Reverse pixel order on a channel. */
__attribute__((import_module("env"), import_name("led_reverse")))
void led_reverse(int channel);

/* ---- LED HSV ---- */

/* Set a single pixel on a channel (1-4) to an HSV color (0-255 each). */
__attribute__((import_module("env"), import_name("led_set_pixel_hsv")))
void led_set_pixel_hsv(int channel, int pos, int h, int s, int v);

/* Fill an entire channel (1-4) with a solid HSV color (0-255 each). */
__attribute__((import_module("env"), import_name("led_fill_hsv")))
void led_fill_hsv(int channel, int h, int s, int v);

/* Convert HSV to packed RGB: returns (r<<16)|(g<<8)|b. */
__attribute__((import_module("env"), import_name("hsv_to_rgb")))
int hsv_to_rgb(int h, int s, int v);

/* Convert RGB to packed HSV: returns (h<<16)|(s<<8)|v (approximate). */
__attribute__((import_module("env"), import_name("rgb_to_hsv")))
int rgb_to_hsv(int r, int g, int b);

/* ---- GPS ---- */

__attribute__((import_module("env"), import_name("get_lat")))
float get_lat(void);

__attribute__((import_module("env"), import_name("get_lon")))
float get_lon(void);

__attribute__((import_module("env"), import_name("get_alt")))
float get_alt(void);

__attribute__((import_module("env"), import_name("get_speed")))
float get_speed(void);

__attribute__((import_module("env"), import_name("get_dir")))
float get_dir(void);

/* Returns 1 if GPS has a fix, 0 otherwise. */
__attribute__((import_module("env"), import_name("gps_valid")))
int gps_valid(void);

/* Returns 1 if GPS hardware is present on this board, 0 otherwise. */
__attribute__((import_module("env"), import_name("gps_present")))
int gps_present(void);

/* ---- GPS Origin / Geometry ---- */

/* Returns the configured origin latitude (from config). */
__attribute__((import_module("env"), import_name("get_origin_lat")))
float get_origin_lat(void);

/* Returns the configured origin longitude (from config). */
__attribute__((import_module("env"), import_name("get_origin_lon")))
float get_origin_lon(void);

/* Returns 1 if GPS has a fix AND an origin is set, 0 otherwise. */
__attribute__((import_module("env"), import_name("has_origin")))
int has_origin(void);

/* Distance in meters from origin to current GPS position. 0 if unavailable. */
__attribute__((import_module("env"), import_name("origin_dist")))
float origin_dist(void);

/* Bearing in degrees from origin to current GPS position. 0 if unavailable. */
__attribute__((import_module("env"), import_name("origin_bearing")))
float origin_bearing(void);

/* ---- IMU ---- */

__attribute__((import_module("env"), import_name("get_roll")))
float get_roll(void);

__attribute__((import_module("env"), import_name("get_pitch")))
float get_pitch(void);

__attribute__((import_module("env"), import_name("get_yaw")))
float get_yaw(void);

__attribute__((import_module("env"), import_name("get_acc_x")))
float get_acc_x(void);

__attribute__((import_module("env"), import_name("get_acc_y")))
float get_acc_y(void);

__attribute__((import_module("env"), import_name("get_acc_z")))
float get_acc_z(void);

/* Returns 1 if IMU data is available, 0 otherwise. */
__attribute__((import_module("env"), import_name("imu_valid")))
int imu_valid(void);

/* Returns 1 if IMU hardware is present on this board, 0 otherwise. */
__attribute__((import_module("env"), import_name("imu_present")))
int imu_present(void);

/* ---- Environment ---- */

/* Temperature in degrees Celsius, or a large negative if no sensor. */
__attribute__((import_module("env"), import_name("get_temp")))
float get_temp(void);

/* Humidity in percent, or -1.0 if no sensor. */
__attribute__((import_module("env"), import_name("get_humidity")))
float get_humidity(void);

/* Brightness 0-4096, or -1.0 if no sensor. */
__attribute__((import_module("env"), import_name("get_brightness")))
float get_brightness(void);

/* Battery voltage (0 if no sensor or not a ConeZ board). */
__attribute__((import_module("env"), import_name("get_bat_voltage")))
float get_bat_voltage(void);

/* Solar panel voltage (0 if no sensor or not a ConeZ board). */
__attribute__((import_module("env"), import_name("get_solar_voltage")))
float get_solar_voltage(void);

/* Battery percentage (0-100), or -1000 if not available. */
__attribute__((import_module("env"), import_name("get_battery_percentage")))
float get_battery_percentage(void);

/* Estimated battery runtime in minutes, or -1000 if not available. */
__attribute__((import_module("env"), import_name("get_battery_runtime")))
float get_battery_runtime(void);

/* ---- Sun Position ---- */

/* Minutes past midnight for sunrise. Returns -1 if sun data not available. */
__attribute__((import_module("env"), import_name("get_sunrise")))
int get_sunrise(void);

/* Minutes past midnight for sunset. Returns -1 if sun data not available. */
__attribute__((import_module("env"), import_name("get_sunset")))
int get_sunset(void);

/* 1 if sun calculation data is valid, 0 otherwise. */
__attribute__((import_module("env"), import_name("sun_valid")))
int sun_valid(void);

/* 1 if between sunrise and sunset, 0 if night, -1 if no data. */
__attribute__((import_module("env"), import_name("is_daylight")))
int is_daylight(void);

/* Sun azimuth in degrees (0=N, 90=E, 180=S, 270=W). -1000 if not available. */
__attribute__((import_module("env"), import_name("get_sun_azimuth")))
float get_sun_azimuth(void);

/* Sun elevation in degrees (-90 to 90). -1000 if not available. */
__attribute__((import_module("env"), import_name("get_sun_elevation")))
float get_sun_elevation(void);

/* ---- Cue Engine ---- */

/* 1 if cue timeline is currently playing, 0 otherwise. */
__attribute__((import_module("env"), import_name("cue_playing")))
int cue_playing(void);

/* Milliseconds elapsed since cue playback started. 0 if not playing. */
__attribute__((import_module("env"), import_name("cue_elapsed")))
int64_t cue_elapsed(void);

/* ---- GPIO ---- */

/* Set a GPIO pin HIGH (configures as OUTPUT). */
__attribute__((import_module("env"), import_name("pin_set")))
void pin_set(int gpio);

/* Set a GPIO pin LOW (configures as OUTPUT). */
__attribute__((import_module("env"), import_name("pin_clear")))
void pin_clear(int gpio);

/* Read a digital GPIO pin (configures as INPUT). Returns 0 or 1. */
__attribute__((import_module("env"), import_name("pin_read")))
int pin_read(int gpio);

/* Read an analog pin (ADC). Returns 0-4095. */
__attribute__((import_module("env"), import_name("analog_read")))
int analog_read(int pin);

/* ---- Time ---- */

/* Milliseconds since Unix epoch (64-bit). Returns 0 if no time source. */
__attribute__((import_module("env"), import_name("get_epoch_ms")))
int64_t get_epoch_ms(void);

/* Milliseconds since boot (wraps at ~49 days). */
__attribute__((import_module("env"), import_name("millis")))
int millis(void);

/* Milliseconds since boot as int64_t (no wrap). */
__attribute__((import_module("env"), import_name("millis64")))
int64_t millis64(void);

/* Delay and yield to FreeRTOS. MUST be called in tight loops. */
__attribute__((import_module("env"), import_name("delay_ms")))
void delay_ms(int ms);

/* Returns 1 if any time source (GPS+PPS or NTP) is active, 0 otherwise. */
__attribute__((import_module("env"), import_name("time_valid")))
int time_valid(void);

/* Milliseconds since boot (64-bit, no wrap). */
__attribute__((import_module("env"), import_name("get_uptime_ms")))
int64_t get_uptime_ms(void);

/* Milliseconds since last LoRa/HTTP communication (0 = boot / not tracked yet). */
__attribute__((import_module("env"), import_name("get_last_comm_ms")))
int64_t get_last_comm_ms(void);

/* ---- Date/Time (calendar fields) ---- */

__attribute__((import_module("env"), import_name("get_year")))
int get_year(void);

__attribute__((import_module("env"), import_name("get_month")))
int get_month(void);

__attribute__((import_module("env"), import_name("get_day")))
int get_day(void);

__attribute__((import_module("env"), import_name("get_hour")))
int get_hour(void);

__attribute__((import_module("env"), import_name("get_minute")))
int get_minute(void);

__attribute__((import_module("env"), import_name("get_second")))
int get_second(void);

/* Day of week: 0=Sunday, 1=Monday, ..., 6=Saturday. */
__attribute__((import_module("env"), import_name("get_day_of_week")))
int get_day_of_week(void);

/* Day of year: 1-366. */
__attribute__((import_module("env"), import_name("get_day_of_year")))
int get_day_of_year(void);

/* Returns 1 if current year is a leap year, 0 otherwise. */
__attribute__((import_module("env"), import_name("get_is_leap_year")))
int get_is_leap_year(void);

/* ---- Params (inter-task communication) ---- */

/* Read a shared parameter (0-15). Param 0 == 1 means "stop requested". */
__attribute__((import_module("env"), import_name("get_param")))
int get_param(int id);

/* Write a shared parameter (0-15). */
__attribute__((import_module("env"), import_name("set_param")))
void set_param(int id, int val);

/* Returns 1 if the host has requested this module to stop. */
__attribute__((import_module("env"), import_name("should_stop")))
int should_stop(void);

/* Return a random integer in [min, max) using ESP32 hardware RNG. */
__attribute__((import_module("env"), import_name("random_int")))
int random_int(int min, int max);

/* ---- Event Synchronization ---- */

/*
 * Wait for a GPS PPS rising edge. Returns 1 on success, 0 on timeout,
 * -1 if GPS has no fix. timeout_ms=0 waits forever.
 */
__attribute__((import_module("env"), import_name("wait_pps")))
int wait_pps(int timeout_ms);

/*
 * Wait for a shared param to match a condition.
 * condition: 0=greater than, 1=less than, 2=equal, 3=not equal.
 * Returns 1 on match, 0 on timeout. timeout_ms=0 waits forever.
 */
__attribute__((import_module("env"), import_name("wait_param")))
int wait_param(int id, int condition, int value, int timeout_ms);

/* Condition constants for wait_param(). */
#define WAIT_GT  0
#define WAIT_LT  1
#define WAIT_EQ  2
#define WAIT_NEQ 3

/* ---- Output ---- */

/* Print a 32-bit integer to the console. */
__attribute__((import_module("env"), import_name("print_i32")))
void print_i32(int val);

/* Print a 32-bit float to the console. */
__attribute__((import_module("env"), import_name("print_f32")))
void print_f32(float val);

/* Print a 64-bit integer to the console. */
__attribute__((import_module("env"), import_name("print_i64")))
void print_i64(long long val);

/* Print a 64-bit double to the console. */
__attribute__((import_module("env"), import_name("print_f64")))
void print_f64(double val);

/* Narrower / unsigned convenience wrappers (route to print_i32 or printf). */
#define print_i8(v)  print_i32((int)(signed char)(v))
#define print_u8(v)  print_i32((int)(unsigned char)(v))
#define print_i16(v) print_i32((int)(short)(v))
#define print_u16(v) print_i32((int)(unsigned short)(v))
#define print_u32(v) printf("%u\n", (unsigned)(v))
#define print_u64(v) printf("%llu\n", (unsigned long long)(v))

/* Print a string (pointer + length) to the console. */
__attribute__((import_module("env"), import_name("print_str")))
void print_str(const char *ptr, int len);

/* ---- LUT (lookup tables) ---- */

/* Load LUT file index (0-255) from flash. Returns entry count, 0 on failure. */
__attribute__((import_module("env"), import_name("lut_load")))
int lut_load(int index);

/* Get value at position in the currently loaded LUT. Returns 0 if out of bounds. */
__attribute__((import_module("env"), import_name("lut_get")))
int lut_get(int index);

/* Return the number of entries in the currently loaded LUT. */
__attribute__((import_module("env"), import_name("lut_size")))
int lut_size(void);

/* Set value at position in the currently loaded LUT (bounds-checked). */
__attribute__((import_module("env"), import_name("lut_set")))
void lut_set(int index, int value);

/* Save the current LUT to flash at the given file index. Returns 1/0. */
__attribute__((import_module("env"), import_name("lut_save")))
int lut_save(int index);

/* Check if a LUT file exists. Returns entry count or -1 if not found. */
__attribute__((import_module("env"), import_name("lut_check")))
int lut_check(int index);

/* ---- File I/O ---- */

/*
 * Open a file on LittleFS. Mode: 0=read, 1=write, 2=append.
 * Path must start with '/', no '..' allowed, /config.ini is protected.
 * Returns a handle (0-3) or -1 on failure. Max 4 files open at once.
 */
__attribute__((import_module("env"), import_name("file_open")))
int file_open(const char *path, int path_len, int mode);

/* Close a file handle. */
__attribute__((import_module("env"), import_name("file_close")))
void file_close(int handle);

/* Read up to max_len bytes into buf. Returns bytes read or -1. */
__attribute__((import_module("env"), import_name("file_read")))
int file_read(int handle, void *buf, int max_len);

/* Write len bytes from buf. Returns bytes written or -1. */
__attribute__((import_module("env"), import_name("file_write")))
int file_write(int handle, const void *buf, int len);

/* Return the total size of the open file, or -1. */
__attribute__((import_module("env"), import_name("file_size")))
int file_size(int handle);

/* Seek to byte position. Returns 1 on success, 0 on failure. */
__attribute__((import_module("env"), import_name("file_seek")))
int file_seek(int handle, int pos);

/* Return the current read/write position, or -1. */
__attribute__((import_module("env"), import_name("file_tell")))
int file_tell(int handle);

/* Check if a file exists. Returns 1 if it exists, 0 otherwise. */
__attribute__((import_module("env"), import_name("file_exists")))
int file_exists(const char *path, int path_len);

/* Delete a file. Returns 1 on success, 0 on failure. */
__attribute__((import_module("env"), import_name("file_delete")))
int file_delete(const char *path, int path_len);

/* Rename a file. Returns 1 on success, 0 on failure. */
__attribute__((import_module("env"), import_name("file_rename")))
int file_rename(const char *old_path, int old_len, const char *new_path, int new_len);

/* Create a directory. Returns 1 on success, 0 on failure. */
__attribute__((import_module("env"), import_name("file_mkdir")))
int file_mkdir(const char *path, int path_len);

/* Remove an empty directory. Returns 1 on success, 0 on failure. */
__attribute__((import_module("env"), import_name("file_rmdir")))
int file_rmdir(const char *path, int path_len);

/* ---- Compression ---- */

/*
 * Decompress a file to another file. Auto-detects gzip/zlib/raw deflate.
 * src/dst are (pointer, length) pairs for the file path strings.
 * Returns decompressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("inflate_file")))
int inflate_file(const char *src, int src_len, const char *dst, int dst_len);

/*
 * Decompress a file into a memory buffer. Auto-detects format.
 * src is (pointer, length) for the file path; dst/dst_max point to WASM memory.
 * Returns decompressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("inflate_file_to_mem")))
int inflate_file_to_mem(const char *src, int src_len, void *dst, int dst_max);

/*
 * Decompress memory to memory. Auto-detects gzip/zlib/raw deflate.
 * Returns decompressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("inflate_mem")))
int inflate_mem(const void *src, int src_len, void *dst, int dst_max);

/*
 * Compress a file to another file (gzip format).
 * src/dst are (pointer, length) pairs for the file path strings.
 * Returns compressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("deflate_file")))
int deflate_file(const char *src, int src_len, const char *dst, int dst_len);

/*
 * Compress a memory buffer to a file (gzip format).
 * src/src_len point to data in WASM memory; dst is (pointer, length) for the file path.
 * Returns compressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("deflate_mem_to_file")))
int deflate_mem_to_file(const void *src, int src_len, const char *dst, int dst_len);

/*
 * Compress memory to memory (gzip format).
 * Returns compressed size on success, -1 on error.
 */
__attribute__((import_module("env"), import_name("deflate_mem")))
int deflate_mem(const void *src, int src_len, void *dst, int dst_max);

/* ---- Math (transcendentals — host-imported, backed by platform libm) ---- */

__attribute__((import_module("env"), import_name("sinf")))
float sinf(float x);

__attribute__((import_module("env"), import_name("cosf")))
float cosf(float x);

__attribute__((import_module("env"), import_name("tanf")))
float tanf(float x);

__attribute__((import_module("env"), import_name("asinf")))
float asinf(float x);

__attribute__((import_module("env"), import_name("acosf")))
float acosf(float x);

__attribute__((import_module("env"), import_name("atanf")))
float atanf(float x);

__attribute__((import_module("env"), import_name("atan2f")))
float atan2f(float y, float x);

__attribute__((import_module("env"), import_name("powf")))
float powf(float base, float exp);

__attribute__((import_module("env"), import_name("expf")))
float expf(float x);

__attribute__((import_module("env"), import_name("logf")))
float logf(float x);

__attribute__((import_module("env"), import_name("log2f")))
float log2f(float x);

__attribute__((import_module("env"), import_name("fmodf")))
float fmodf(float x, float y);

/* ---- Math (double-precision transcendentals — host-imported) ---- */

__attribute__((import_module("env"), import_name("sin")))
double sin(double x);

__attribute__((import_module("env"), import_name("cos")))
double cos(double x);

__attribute__((import_module("env"), import_name("tan")))
double tan(double x);

__attribute__((import_module("env"), import_name("asin")))
double asin(double x);

__attribute__((import_module("env"), import_name("acos")))
double acos(double x);

__attribute__((import_module("env"), import_name("atan")))
double atan(double x);

__attribute__((import_module("env"), import_name("atan2")))
double atan2(double y, double x);

__attribute__((import_module("env"), import_name("pow")))
double pow(double base, double exp);

__attribute__((import_module("env"), import_name("exp")))
double exp(double x);

__attribute__((import_module("env"), import_name("log")))
double log(double x);

__attribute__((import_module("env"), import_name("log2")))
double log2(double x);

__attribute__((import_module("env"), import_name("fmod")))
double fmod(double x, double y);

/* ---- Math (WASM-native — compile to single instructions, zero overhead) ---- */

static inline float sqrtf(float x)  { return __builtin_sqrtf(x); }
static inline float fabsf(float x)  { return __builtin_fabsf(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline float ceilf(float x)  { return __builtin_ceilf(x); }
static inline float truncf(float x) { return __builtin_truncf(x); }
static inline float fminf(float a, float b) { return __builtin_fminf(a, b); }
static inline float fmaxf(float a, float b) { return __builtin_fmaxf(a, b); }

static inline double sqrt(double x)  { return __builtin_sqrt(x); }
static inline double fabs(double x)  { return __builtin_fabs(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline double ceil(double x)  { return __builtin_ceil(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline double fmin(double a, double b) { return __builtin_fmin(a, b); }
static inline double fmax(double a, double b) { return __builtin_fmax(a, b); }

/* ---- Curve / Interpolation ---- */

/* Linear interpolation: returns a + t * (b - a). */
__attribute__((import_module("env"), import_name("lerp")))
float lerp(float a, float b, float t);

/*
 * Smoothed clamped linear interpolation (integer).
 * Maps x_pos from [x_min, x_max] to [min_val, max_val].
 *   offset:  percentage (0-100) of half-range to shrink from each end
 *   window:  percentage (0-100) of offset region used as smoothing width
 *   stride:  step size for samples within the smoothing window
 */
__attribute__((import_module("env"), import_name("larp")))
int larp(int x_pos, int x_min, int x_max, int min_val, int max_val,
         int offset, int window, int stride);

/*
 * Smoothed clamped linear interpolation (float).
 * Same as larp but with float params. stride is the number of
 * subdivisions of the smoothing window.
 */
__attribute__((import_module("env"), import_name("larpf")))
float larpf(float x_pos, float x_min, float x_max, float min_val, float max_val,
            float offset, float window, int stride);

/* M_PI constant */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Memory and string functions (compiled into your module) ---- */
/*
 * memcpy/memset/memmove compile to WASM bulk-memory instructions
 * (memory.copy / memory.fill) when built with -mbulk-memory.
 * String functions are simple inline loops over linear memory.
 */

typedef __SIZE_TYPE__ size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/*
 * Host-backed allocator helpers operating on WASM linear memory.
 * These keep module code size small compared to embedding a full allocator.
 */
__attribute__((import_module("env"), import_name("malloc")))
void *malloc(size_t size);

__attribute__((import_module("env"), import_name("free")))
void free(void *ptr);

__attribute__((import_module("env"), import_name("calloc")))
void *calloc(size_t nmemb, size_t size);

__attribute__((import_module("env"), import_name("realloc")))
void *realloc(void *ptr, size_t size);

static inline void *memcpy(void *dst, const void *src, size_t n) {
    return __builtin_memcpy(dst, src, n);
}

static inline void *memset(void *dst, int c, size_t n) {
    return __builtin_memset(dst, c, n);
}

static inline void *memmove(void *dst, const void *src, size_t n) {
    return __builtin_memmove(dst, src, n);
}

static inline int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

static inline size_t strlen(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static inline int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

static inline int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : (char *)NULL;
}

/* ---- Printf family ---- */
/*
 * Default: host-imported format engine (~50 bytes per module).
 * Define CONEZ_PRINTF_INLINE before including this header to use the
 * fully self-contained inline implementation (~1KB per module) instead.
 *
 * Both support: %d %i %u %x %X %c %s %f %p %%
 * Flags: - 0   Width: number or *   Precision: .number or .*
 * Length: l/ll/h/hh/z (accepted, ignored — 32-bit platform)
 */

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap)         __builtin_va_end(ap)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_copy(dst, src)  __builtin_va_copy(dst, src)

#ifdef CONEZ_PRINTF_INLINE

/* ---- Inline printf (self-contained, no host dependency) ---- */
/* Float: %f only, precision 0-9, integer part up to 2^32-1. */

static inline int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    size_t pos = 0;
    #define _PF_OUT(c)     do { if (pos + 1 < size) buf[pos] = (c); pos++; } while (0)
    #define _PF_PAD(ch, n) do { for (int _i = 0; _i < (n); _i++) _PF_OUT(ch); } while (0)

    while (*fmt) {
        if (*fmt != '%') { _PF_OUT(*fmt++); continue; }
        fmt++;

        /* flags */
        int fl_left = 0, fl_zero = 0;
        for (;;) {
            if      (*fmt == '-') fl_left = 1;
            else if (*fmt == '0') fl_zero = 1;
            else break;
            fmt++;
        }
        if (fl_left) fl_zero = 0;

        /* width */
        int w = 0;
        if (*fmt == '*') {
            w = va_arg(ap, int);
            if (w < 0) { fl_left = 1; w = -w; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') w = w * 10 + (*fmt++ - '0');
        }

        /* precision */
        int p = -1;
        if (*fmt == '.') {
            fmt++; p = 0;
            if (*fmt == '*') { p = va_arg(ap, int); if (p < 0) p = 0; fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') p = p * 10 + (*fmt++ - '0');
        }

        /* length modifier — accept and skip */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't')
            fmt++;

        char spec = *fmt;
        if (spec) fmt++;

        switch (spec) {
        case '\0': break;
        case '%': _PF_OUT('%'); break;

        case 'c': {
            char ch = (char)va_arg(ap, int);
            if (!fl_left) _PF_PAD(' ', w - 1);
            _PF_OUT(ch);
            if (fl_left)  _PF_PAD(' ', w - 1);
            break;
        }

        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int sn = 0;
            while (s[sn] && (p < 0 || sn < p)) sn++;
            if (!fl_left) _PF_PAD(' ', w - sn);
            for (int i = 0; i < sn; i++) _PF_OUT(s[i]);
            if (fl_left)  _PF_PAD(' ', w - sn);
            break;
        }

        case 'd': case 'i': case 'u': {
            unsigned int uv;
            int neg = 0;
            if (spec == 'u') {
                uv = va_arg(ap, unsigned int);
            } else {
                int sv = va_arg(ap, int);
                if (sv < 0) { neg = 1; uv = (unsigned int)(-(sv + 1)) + 1u; }
                else uv = (unsigned int)sv;
            }
            char tmp[11]; int len = 0;
            do { tmp[len++] = '0' + (char)(uv % 10); uv /= 10; } while (uv);
            int total = len + neg;
            if (!fl_left) {
                if (fl_zero) { if (neg) _PF_OUT('-'); _PF_PAD('0', w - total); }
                else         { _PF_PAD(' ', w - total); if (neg) _PF_OUT('-'); }
            } else { if (neg) _PF_OUT('-'); }
            for (int i = len - 1; i >= 0; i--) _PF_OUT(tmp[i]);
            if (fl_left) _PF_PAD(' ', w - total);
            break;
        }

        case 'x': case 'X': {
            unsigned int uv = va_arg(ap, unsigned int);
            const char *xd = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            char tmp[8]; int len = 0;
            do { tmp[len++] = xd[uv & 0xF]; uv >>= 4; } while (uv);
            if (!fl_left) _PF_PAD(fl_zero ? '0' : ' ', w - len);
            for (int i = len - 1; i >= 0; i--) _PF_OUT(tmp[i]);
            if (fl_left) _PF_PAD(' ', w - len);
            break;
        }

        case 'p': {
            unsigned int uv = (unsigned int)(size_t)va_arg(ap, void *);
            _PF_OUT('0'); _PF_OUT('x');
            char tmp[8]; int len = 0;
            do { tmp[len++] = "0123456789abcdef"[uv & 0xF]; uv >>= 4; } while (uv);
            for (int i = len - 1; i >= 0; i--) _PF_OUT(tmp[i]);
            break;
        }

        case 'f': {
            double v = va_arg(ap, double);
            if (v != v) {
                if (!fl_left) _PF_PAD(' ', w - 3);
                _PF_OUT('n'); _PF_OUT('a'); _PF_OUT('n');
                if (fl_left) _PF_PAD(' ', w - 3);
                break;
            }
            int neg = 0;
            if (v < 0) { neg = 1; v = -v; }
            if (v > 4294967295.0) {
                int ilen = neg ? 4 : 3;
                if (!fl_left) _PF_PAD(' ', w - ilen);
                if (neg) _PF_OUT('-');
                _PF_OUT('i'); _PF_OUT('n'); _PF_OUT('f');
                if (fl_left) _PF_PAD(' ', w - ilen);
                break;
            }
            int dp = (p >= 0) ? p : 6;
            if (dp > 9) dp = 9;
            double rnd = 0.5;
            for (int i = 0; i < dp; i++) rnd *= 0.1;
            v += rnd;
            unsigned int ipart = (unsigned int)v;
            double frac = v - (double)ipart;
            char tmp[11]; int ilen = 0;
            do { tmp[ilen++] = '0' + (char)(ipart % 10); ipart /= 10; } while (ipart);
            int total = neg + ilen + (dp > 0 ? 1 + dp : 0);
            if (!fl_left) {
                if (fl_zero) { if (neg) _PF_OUT('-'); _PF_PAD('0', w - total); }
                else         { _PF_PAD(' ', w - total); if (neg) _PF_OUT('-'); }
            } else { if (neg) _PF_OUT('-'); }
            for (int i = ilen - 1; i >= 0; i--) _PF_OUT(tmp[i]);
            if (dp > 0) {
                _PF_OUT('.');
                for (int i = 0; i < dp; i++) {
                    frac *= 10.0;
                    int d = (int)frac;
                    if (d > 9) d = 9;
                    _PF_OUT('0' + (char)d);
                    frac -= d;
                }
            }
            if (fl_left) _PF_PAD(' ', w - total);
            break;
        }

        default: _PF_OUT('%'); _PF_OUT(spec); break;
        }
    }

    if (size > 0) buf[pos < size ? pos : size - 1] = '\0';
    #undef _PF_OUT
    #undef _PF_PAD
    return (int)pos;
}

static inline int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap); return r;
}

static inline int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap); return r;
}

static inline int printf(const char *fmt, ...) {
    char tmp[256];
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    print_str(tmp, r < 255 ? r : 255);
    return r;
}

#else /* Host-imported format engine (default) */

/*
 * Formatting runs on the ESP32 host via snprintf. The module passes its
 * format string pointer and va_list pointer; the host reads both from WASM
 * linear memory. Assumes wasm32 clang va_list layout (4-byte-aligned args).
 */

__attribute__((import_module("env"), import_name("host_printf")))
int _host_printf(const char *fmt, const void *args);

__attribute__((import_module("env"), import_name("host_snprintf")))
int _host_snprintf(char *buf, int size, const char *fmt, const void *args);

static inline int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    return _host_snprintf(buf, (int)size, fmt, (const void *)ap);
}

static inline int snprintf(char *buf, size_t n, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = _host_snprintf(buf, (int)n, fmt, (const void *)ap);
    va_end(ap); return r;
}

static inline int sprintf(char *buf, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = _host_snprintf(buf, 0x7FFFFFFF, fmt, (const void *)ap);
    va_end(ap); return r;
}

static inline int printf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = _host_printf(fmt, (const void *)ap);
    va_end(ap); return r;
}

#endif /* CONEZ_PRINTF_INLINE */

/* ---- sscanf (always host-imported) ---- */

__attribute__((import_module("env"), import_name("host_sscanf")))
int _host_sscanf(const char *str, const char *fmt, const void *args);

static inline int sscanf(const char *str, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int r = _host_sscanf(str, fmt, (const void *)ap);
    va_end(ap); return r;
}

/* ---- Helpers (not imports, compiled into your module) ---- */

static inline void print(const char *s) {
    print_str(s, (int)strlen(s));
}

static inline int puts(const char *s) {
    print_str(s, (int)strlen(s));
    print_str("\n", 1);
    return 0;
}

static inline int putchar(int c) {
    char ch = (char)c;
    print_str(&ch, 1);
    return c;
}

/* Unpack RGB from packed int returned by hsv_to_rgb(). */
static inline int rgb_r(int packed) { return (packed >> 16) & 0xFF; }
static inline int rgb_g(int packed) { return (packed >>  8) & 0xFF; }
static inline int rgb_b(int packed) { return  packed        & 0xFF; }

/* Unpack HSV from packed int returned by rgb_to_hsv(). */
static inline int hsv_h(int packed) { return (packed >> 16) & 0xFF; }
static inline int hsv_s(int packed) { return (packed >>  8) & 0xFF; }
static inline int hsv_v(int packed) { return  packed        & 0xFF; }

/* Open a file by C string (convenience wrapper). */
static inline int file_open_str(const char *path, int mode) {
    return file_open(path, (int)strlen(path), mode);
}

/* File helper convenience wrappers (C string versions). */
static inline int file_exists_str(const char *path) {
    return file_exists(path, (int)strlen(path));
}
static inline int file_delete_str(const char *path) {
    return file_delete(path, (int)strlen(path));
}
static inline int file_rename_str(const char *old_path, const char *new_path) {
    return file_rename(old_path, (int)strlen(old_path), new_path, (int)strlen(new_path));
}
static inline int file_mkdir_str(const char *path) {
    return file_mkdir(path, (int)strlen(path));
}
static inline int file_rmdir_str(const char *path) {
    return file_rmdir(path, (int)strlen(path));
}

/* Inflate convenience wrappers (C string path versions). */
static inline int inflate_file_str(const char *src, const char *dst) {
    return inflate_file(src, (int)strlen(src), dst, (int)strlen(dst));
}
static inline int inflate_file_to_mem_str(const char *src, void *dst, int dst_max) {
    return inflate_file_to_mem(src, (int)strlen(src), dst, dst_max);
}

/* ---- Math / Utility Helpers ---- */

/* Clamp an integer to [lo, hi]. */
static inline int clamp(int val, int lo, int hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* Clamp an integer to [0, 255]. */
static inline int clamp255(int val) {
    if (val < 0) return 0;
    if (val > 255) return 255;
    return val;
}

/* Integer absolute value. */
static inline int abs_i(int val) {
    return val < 0 ? -val : val;
}

/* Map a value from one range to another (integer). */
static inline int map_range(int val, int in_min, int in_max, int out_min, int out_max) {
    if (in_max == in_min) return out_min;
    return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/*
 * Sine using 0-255 range: input 0-255 maps to 0-360 degrees,
 * output 0-255 (0=mid, 128=mid, values oscillate sinusoidally).
 * Equivalent to BASIC's SIN256().
 */
static inline int sin256(int val) {
    float rad = ((float)(val & 0xFF) / 255.0f) * 2.0f * M_PI;
    float s = sinf(rad);
    return (int)((s + 1.0f) * 0.5f * 255.0f);
}

/* ---- C Standard Library Helpers ---- */

static inline int atoi(const char *s) {
    int val = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return neg ? -val : val;
}

static inline long strtol(const char *s, char **endptr, int base) {
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    long val = 0;
    const char *start = s;
    while (*s) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endptr) *endptr = (char *)(s == start ? (const char *)s - (s != start) : s);
    return neg ? -val : val;
}

static inline float strtof(const char *s, char **endptr) {
    float val = 0;
    sscanf(s, "%f", &val);
    if (endptr) {
        /* Advance past consumed chars */
        const char *p = s;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '-' || *p == '+') p++;
        while ((*p >= '0' && *p <= '9') || *p == '.') p++;
        if (*p == 'e' || *p == 'E') { p++; if (*p == '-' || *p == '+') p++; while (*p >= '0' && *p <= '9') p++; }
        *endptr = (char *)p;
    }
    return val;
}

static inline float atof(const char *s) {
    return strtof(s, (char **)0);
}

#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

#endif /* CONEZ_API_H */
