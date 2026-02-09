//! ConeZ WASM API — Rust bindings
//!
//! Rust equivalent of `conez_api.h`. All functions are imported from the
//! "env" module by the ConeZ wasm3 runtime.
//!
//! Usage (no_std):
//!   ```ignore
//!   include!("../../conez_api.rs");  // adjust path to your module location
//!   ```
//!
//! Or copy the `extern "C"` block into your lib.rs and keep only the
//! functions you actually call — the linker will ignore unused imports.
//!
//! Entry points (export one or both):
//!   ```ignore
//!   #[no_mangle] pub extern "C" fn setup() { ... }
//!   #[export_name = "loop"] pub extern "C" fn wasm_loop() { ... }
//!   ```

extern "C" {
    // ---- LED ----
    pub fn led_set_pixel(channel: i32, pos: i32, r: i32, g: i32, b: i32);
    pub fn led_fill(channel: i32, r: i32, g: i32, b: i32);
    pub fn led_show();
    pub fn led_count(channel: i32) -> i32;
    pub fn led_gamma8(val: i32) -> i32;
    pub fn led_set_gamma(enable: i32);
    pub fn led_set_buffer(channel: i32, rgb_data: *const u8, count: i32);
    pub fn led_shift(channel: i32, amount: i32, r: i32, g: i32, b: i32);
    pub fn led_rotate(channel: i32, amount: i32);
    pub fn led_reverse(channel: i32);

    // ---- LED HSV ----
    pub fn led_set_pixel_hsv(channel: i32, pos: i32, h: i32, s: i32, v: i32);
    pub fn led_fill_hsv(channel: i32, h: i32, s: i32, v: i32);
    pub fn hsv_to_rgb(h: i32, s: i32, v: i32) -> i32;
    pub fn rgb_to_hsv(r: i32, g: i32, b: i32) -> i32;

    // ---- GPS ----
    pub fn get_lat() -> f32;
    pub fn get_lon() -> f32;
    pub fn get_alt() -> f32;
    pub fn get_speed() -> f32;
    pub fn get_dir() -> f32;
    /// Returns 1 if GPS has a fix, 0 otherwise.
    pub fn gps_valid() -> i32;
    /// Returns 1 if GPS hardware is present on this board, 0 otherwise.
    pub fn gps_present() -> i32;

    // ---- GPS Origin / Geometry ----
    pub fn get_origin_lat() -> f32;
    pub fn get_origin_lon() -> f32;
    /// Returns 1 if GPS has a fix AND an origin is set.
    pub fn has_origin() -> i32;
    /// Distance in meters from origin to current GPS position.
    pub fn origin_dist() -> f32;
    /// Bearing in degrees from origin to current GPS position.
    pub fn origin_bearing() -> f32;

    // ---- IMU ----
    pub fn get_roll() -> f32;
    pub fn get_pitch() -> f32;
    pub fn get_yaw() -> f32;
    pub fn get_acc_x() -> f32;
    pub fn get_acc_y() -> f32;
    pub fn get_acc_z() -> f32;
    /// Returns 1 if IMU data is available, 0 otherwise.
    pub fn imu_valid() -> i32;
    /// Returns 1 if IMU hardware is present on this board, 0 otherwise.
    pub fn imu_present() -> i32;

    // ---- Environment ----
    /// Temperature in degrees Celsius, or a large negative if no sensor.
    pub fn get_temp() -> f32;
    /// Humidity in percent, or -1.0 if no sensor.
    pub fn get_humidity() -> f32;
    /// Brightness 0-4096, or -1.0 if no sensor.
    pub fn get_brightness() -> f32;

    // ---- Battery / Solar ----
    /// Battery voltage (0 if no sensor).
    pub fn get_bat_voltage() -> f32;
    /// Solar panel voltage (0 if no sensor).
    pub fn get_solar_voltage() -> f32;
    /// Battery percentage (0-100), or -1000.0 if not available.
    pub fn get_battery_percentage() -> f32;
    /// Estimated battery runtime in minutes, or -1000.0 if not available.
    pub fn get_battery_runtime() -> f32;

    // ---- Sun Position ----
    /// Minutes past midnight for sunrise. Returns -1 if not available.
    pub fn get_sunrise() -> i32;
    /// Minutes past midnight for sunset. Returns -1 if not available.
    pub fn get_sunset() -> i32;
    /// 1 if sun calculation data is valid, 0 otherwise.
    pub fn sun_valid() -> i32;
    /// 1 if between sunrise and sunset, 0 if night, -1 if no data.
    pub fn is_daylight() -> i32;
    /// Sun azimuth in degrees (0=N, 90=E, 180=S, 270=W). -1000.0 if not available.
    pub fn get_sun_azimuth() -> f32;
    /// Sun elevation in degrees (-90 to 90). -1000.0 if not available.
    pub fn get_sun_elevation() -> f32;

    // ---- Cue Engine ----
    /// 1 if cue timeline is currently playing, 0 otherwise.
    pub fn cue_playing() -> i32;
    /// Milliseconds elapsed since cue playback started. 0 if not playing.
    pub fn cue_elapsed() -> i32;

    // ---- GPIO ----
    pub fn pin_set(gpio: i32);
    pub fn pin_clear(gpio: i32);
    pub fn pin_read(gpio: i32) -> i32;
    pub fn analog_read(pin: i32) -> i32;

    // ---- Time ----
    /// Milliseconds since Unix epoch (64-bit). Returns 0 if no time source.
    pub fn get_epoch_ms() -> i64;
    /// Milliseconds since boot (wraps at ~49 days).
    pub fn millis() -> i32;
    /// Delay and yield to FreeRTOS. MUST be called in tight loops.
    pub fn delay_ms(ms: i32);
    /// Returns 1 if any time source (GPS+PPS or NTP) is active.
    pub fn time_valid() -> i32;
    /// Milliseconds since boot (64-bit, no wrap).
    pub fn get_uptime_ms() -> i64;
    /// Milliseconds since last LoRa/HTTP communication (0 = not tracked yet).
    pub fn get_last_comm_ms() -> i64;

    // ---- Date/Time ----
    pub fn get_year() -> i32;
    pub fn get_month() -> i32;
    pub fn get_day() -> i32;
    pub fn get_hour() -> i32;
    pub fn get_minute() -> i32;
    pub fn get_second() -> i32;
    /// Day of week: 0=Sunday .. 6=Saturday.
    pub fn get_day_of_week() -> i32;
    /// Day of year: 1-366.
    pub fn get_day_of_year() -> i32;
    /// Returns 1 if current year is a leap year.
    pub fn get_is_leap_year() -> i32;

    // ---- Params ----
    /// Read a shared parameter (0-15). Param 0 == 1 means "stop requested".
    pub fn get_param(id: i32) -> i32;
    pub fn set_param(id: i32, val: i32);
    /// Returns 1 if the host has requested this module to stop.
    pub fn should_stop() -> i32;
    /// Random integer in [min, max) using ESP32 hardware RNG.
    pub fn random_int(min: i32, max: i32) -> i32;

    // ---- Event Synchronization ----
    /// Wait for GPS PPS rising edge. Returns 1/0/-1.
    pub fn wait_pps(timeout_ms: i32) -> i32;
    /// Wait for a shared param to match a condition.
    pub fn wait_param(id: i32, condition: i32, value: i32, timeout_ms: i32) -> i32;

    // ---- Output ----
    pub fn print_i32(val: i32);
    pub fn print_f32(val: f32);
    pub fn print_i64(val: i64);
    pub fn print_f64(val: f64);
    pub fn print_str(ptr: *const u8, len: i32);

    // ---- LUT ----
    pub fn lut_load(index: i32) -> i32;
    pub fn lut_get(index: i32) -> i32;
    pub fn lut_size() -> i32;
    pub fn lut_set(index: i32, value: i32);
    pub fn lut_save(index: i32) -> i32;
    pub fn lut_check(index: i32) -> i32;

    // ---- File I/O ----
    /// Open a file. Mode: 0=read, 1=write, 2=append. Returns handle (0-3) or -1.
    pub fn file_open(path: *const u8, path_len: i32, mode: i32) -> i32;
    pub fn file_close(handle: i32);
    pub fn file_read(handle: i32, buf: *mut u8, max_len: i32) -> i32;
    pub fn file_write(handle: i32, buf: *const u8, len: i32) -> i32;
    pub fn file_size(handle: i32) -> i32;
    pub fn file_seek(handle: i32, pos: i32) -> i32;
    pub fn file_tell(handle: i32) -> i32;
    pub fn file_exists(path: *const u8, path_len: i32) -> i32;
    pub fn file_delete(path: *const u8, path_len: i32) -> i32;
    pub fn file_rename(old_path: *const u8, old_len: i32, new_path: *const u8, new_len: i32) -> i32;

    // ---- Math (host-imported transcendentals) ----
    pub fn sinf(x: f32) -> f32;
    pub fn cosf(x: f32) -> f32;
    pub fn tanf(x: f32) -> f32;
    pub fn asinf(x: f32) -> f32;
    pub fn acosf(x: f32) -> f32;
    pub fn atanf(x: f32) -> f32;
    pub fn atan2f(y: f32, x: f32) -> f32;
    pub fn powf(base: f32, exp: f32) -> f32;
    pub fn expf(x: f32) -> f32;
    pub fn logf(x: f32) -> f32;
    pub fn log2f(x: f32) -> f32;
    pub fn fmodf(x: f32, y: f32) -> f32;

    // ---- Printf (host-imported) ----
    pub fn host_printf(fmt: *const u8, args: *const u8) -> i32;
    pub fn host_snprintf(buf: *mut u8, size: i32, fmt: *const u8, args: *const u8) -> i32;
    pub fn host_sscanf(str: *const u8, fmt: *const u8, args: *const u8) -> i32;
}

/// Wait-condition constants for `wait_param()`.
pub const WAIT_GT: i32 = 0;
pub const WAIT_LT: i32 = 1;
pub const WAIT_EQ: i32 = 2;
pub const WAIT_NEQ: i32 = 3;

// ---- Convenience helpers (safe wrappers) ----

/// Print a Rust string slice to the ConeZ console.
#[inline]
pub fn print(s: &str) {
    unsafe { print_str(s.as_ptr(), s.len() as i32) }
}

/// Print a Rust string slice followed by a newline.
#[inline]
pub fn println(s: &str) {
    unsafe {
        print_str(s.as_ptr(), s.len() as i32);
        print_str(b"\n".as_ptr(), 1);
    }
}

/// Unpack red from packed RGB int (returned by `hsv_to_rgb`).
#[inline]
pub const fn rgb_r(packed: i32) -> i32 { (packed >> 16) & 0xFF }
/// Unpack green from packed RGB int.
#[inline]
pub const fn rgb_g(packed: i32) -> i32 { (packed >> 8) & 0xFF }
/// Unpack blue from packed RGB int.
#[inline]
pub const fn rgb_b(packed: i32) -> i32 { packed & 0xFF }

/// Open a file by path string. Mode: 0=read, 1=write, 2=append.
#[inline]
pub fn file_open_str(path: &str, mode: i32) -> i32 {
    unsafe { file_open(path.as_ptr(), path.len() as i32, mode) }
}

/// Check if a file exists by path string.
#[inline]
pub fn file_exists_str(path: &str) -> bool {
    unsafe { file_exists(path.as_ptr(), path.len() as i32) != 0 }
}

/// Delete a file by path string.
#[inline]
pub fn file_delete_str(path: &str) -> bool {
    unsafe { file_delete(path.as_ptr(), path.len() as i32) != 0 }
}
