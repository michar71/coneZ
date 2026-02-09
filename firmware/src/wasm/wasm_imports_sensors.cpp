#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "board.h"
#include "main.h"
#include "gps.h"
#include "sensors.h"
#include "sun.h"

// --- GPS ---

// f32 get_lat()
m3ApiRawFunction(m3_get_lat)
{
    m3ApiReturnType(float);
    m3ApiReturn(get_lat());
}

// f32 get_lon()
m3ApiRawFunction(m3_get_lon)
{
    m3ApiReturnType(float);
    m3ApiReturn(get_lon());
}

// f32 get_alt()
m3ApiRawFunction(m3_get_alt)
{
    m3ApiReturnType(float);
    m3ApiReturn(get_alt());
}

// f32 get_speed()
m3ApiRawFunction(m3_get_speed)
{
    m3ApiReturnType(float);
    m3ApiReturn(get_speed());
}

// f32 get_dir()
m3ApiRawFunction(m3_get_dir)
{
    m3ApiReturnType(float);
    m3ApiReturn(get_dir());
}

// i32 gps_valid()
m3ApiRawFunction(m3_gps_valid)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_gpsstatus() ? 1 : 0);
}

// --- IMU ---

// f32 get_roll()
m3ApiRawFunction(m3_get_roll)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getRoll() : 0.0f);
}

// f32 get_pitch()
m3ApiRawFunction(m3_get_pitch)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getPitch() : 0.0f);
}

// f32 get_yaw()
m3ApiRawFunction(m3_get_yaw)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getYaw() : 0.0f);
}

// f32 get_acc_x()
m3ApiRawFunction(m3_get_acc_x)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getAccX() : 0.0f);
}

// f32 get_acc_y()
m3ApiRawFunction(m3_get_acc_y)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getAccY() : 0.0f);
}

// f32 get_acc_z()
m3ApiRawFunction(m3_get_acc_z)
{
    m3ApiReturnType(float);
    m3ApiReturn(imuAvailable() ? getAccZ() : 0.0f);
}

// i32 imu_valid()
m3ApiRawFunction(m3_imu_valid)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn(imuAvailable() ? 1 : 0);
}

// --- Environment ---

// f32 get_temp()
m3ApiRawFunction(m3_get_temp)
{
    m3ApiReturnType(float);
    m3ApiReturn(getTemp());
}

m3ApiRawFunction(m3_get_humidity) {
    m3ApiReturnType(float);
    m3ApiReturn(-1.0f);     // no humidity sensor on current hardware
}
m3ApiRawFunction(m3_get_brightness) {
    m3ApiReturnType(float);
    m3ApiReturn(-1.0f);     // no brightness sensor on current hardware
}

// --- Battery / Solar ---

// f32 get_bat_voltage()
m3ApiRawFunction(m3_get_bat_voltage) {
    m3ApiReturnType(float);
    m3ApiReturn(bat_voltage());
}

// f32 get_solar_voltage()
m3ApiRawFunction(m3_get_solar_voltage) {
    m3ApiReturnType(float);
    m3ApiReturn(solar_voltage());
}

// --- Sun position ---

// i32 get_sunrise() — minutes past midnight, -1 if invalid
m3ApiRawFunction(m3_get_sunrise) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(sunRise());
}

// i32 get_sunset() — minutes past midnight, -1 if invalid
m3ApiRawFunction(m3_get_sunset) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(sunSet());
}

// i32 sun_valid() — 1 if sun data is valid
m3ApiRawFunction(m3_sun_valid) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(sunDataIsValid() ? 1 : 0);
}

// i32 is_daylight() — 1 if currently between sunrise and sunset
m3ApiRawFunction(m3_is_daylight) {
    m3ApiReturnType(int32_t);
    if (!sunDataIsValid()) m3ApiReturn(-1);
    int now_min = get_hour() * 60 + get_minute();
    int rise = sunRise();
    int set = sunSet();
    if (rise < 0 || set < 0) m3ApiReturn(-1);
    m3ApiReturn((now_min >= rise && now_min < set) ? 1 : 0);
}

// --- Hardware presence ---

// i32 gps_present()
m3ApiRawFunction(m3_gps_present) {
    m3ApiReturnType(int32_t);
#ifdef BOARD_HAS_GPS
    m3ApiReturn(1);
#else
    m3ApiReturn(0);
#endif
}

// i32 imu_present()
m3ApiRawFunction(m3_imu_present) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(imuAvailable() ? 1 : 0);
}

// --- Battery percentage / runtime ---

// f32 get_battery_percentage()
// FIXME: implement real LiPo voltage-to-percentage lookup
m3ApiRawFunction(m3_get_battery_percentage) {
    m3ApiReturnType(float);
    float v = bat_voltage();
    if (v < 0.01f) m3ApiReturn(-1000.0f);  // no battery
    m3ApiReturn(-1000.0f);  // FIXME: placeholder until battery curve is calibrated
}

// f32 get_battery_runtime()
// FIXME: needs current sensing hardware to estimate runtime
m3ApiRawFunction(m3_get_battery_runtime) {
    m3ApiReturnType(float);
    m3ApiReturn(-1000.0f);  // not available
}

// --- Sun azimuth / elevation ---

// f32 get_sun_azimuth() — degrees from north, -1000 if invalid
m3ApiRawFunction(m3_get_sun_azimuth) {
    m3ApiReturnType(float);
    m3ApiReturn(sunAzimuth());
}

// f32 get_sun_elevation() — degrees, -1000 if invalid
m3ApiRawFunction(m3_get_sun_elevation) {
    m3ApiReturnType(float);
    m3ApiReturn(sunElevation());
}

// --- Origin / Geometry ---

m3ApiRawFunction(m3_get_origin_lat) {
    m3ApiReturnType(float);
    m3ApiReturn(get_org_lat());
}
m3ApiRawFunction(m3_get_origin_lon) {
    m3ApiReturnType(float);
    m3ApiReturn(get_org_lon());
}
m3ApiRawFunction(m3_has_origin) {
    m3ApiReturnType(int32_t);
    float olat = get_org_lat(), olon = get_org_lon();
    m3ApiReturn((get_gpsstatus() && (olat != 0.0f || olon != 0.0f)) ? 1 : 0);
}
m3ApiRawFunction(m3_origin_dist) {
    m3ApiReturnType(float);
    float olat = get_org_lat(), olon = get_org_lon();
    float lat = get_lat(), lon = get_lon();
    if (!get_gpsstatus() || (olat == 0.0f && olon == 0.0f)) m3ApiReturn(0.0f);
    float x1, y1, x2, y2;
    latlon_to_meters(olat, olon, &x1, &y1);
    latlon_to_meters(lat, lon, &x2, &y2);
    GeoResult gr = xy_to_polar(x1, y1, x2, y2);
    m3ApiReturn(gr.distance);
}
m3ApiRawFunction(m3_origin_bearing) {
    m3ApiReturnType(float);
    float olat = get_org_lat(), olon = get_org_lon();
    float lat = get_lat(), lon = get_lon();
    if (!get_gpsstatus() || (olat == 0.0f && olon == 0.0f)) m3ApiReturn(0.0f);
    float x1, y1, x2, y2;
    latlon_to_meters(olat, olon, &x1, &y1);
    latlon_to_meters(lat, lon, &x2, &y2);
    GeoResult gr = xy_to_polar(x1, y1, x2, y2);
    m3ApiReturn(gr.bearing_deg);
}


// ---------- Link sensor imports ----------

M3Result link_sensor_imports(IM3Module module)
{
    M3Result result;

    // GPS
    result = m3_LinkRawFunction(module, "env", "get_lat", "f()", m3_get_lat);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_lon", "f()", m3_get_lon);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_alt", "f()", m3_get_alt);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_speed", "f()", m3_get_speed);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_dir", "f()", m3_get_dir);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "gps_valid", "i()", m3_gps_valid);
    if (result && result != m3Err_functionLookupFailed) return result;

    // IMU
    result = m3_LinkRawFunction(module, "env", "get_roll", "f()", m3_get_roll);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_pitch", "f()", m3_get_pitch);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_yaw", "f()", m3_get_yaw);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_acc_x", "f()", m3_get_acc_x);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_acc_y", "f()", m3_get_acc_y);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_acc_z", "f()", m3_get_acc_z);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "imu_valid", "i()", m3_imu_valid);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Environment
    result = m3_LinkRawFunction(module, "env", "get_temp", "f()", m3_get_temp);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_humidity", "f()", m3_get_humidity);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_brightness", "f()", m3_get_brightness);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Battery / Solar
    result = m3_LinkRawFunction(module, "env", "get_bat_voltage", "f()", m3_get_bat_voltage);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_solar_voltage", "f()", m3_get_solar_voltage);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Sun
    result = m3_LinkRawFunction(module, "env", "get_sunrise", "i()", m3_get_sunrise);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_sunset", "i()", m3_get_sunset);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "sun_valid", "i()", m3_sun_valid);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "is_daylight", "i()", m3_is_daylight);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Origin / Geometry
    result = m3_LinkRawFunction(module, "env", "get_origin_lat", "f()", m3_get_origin_lat);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_origin_lon", "f()", m3_get_origin_lon);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "has_origin", "i()", m3_has_origin);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "origin_dist", "f()", m3_origin_dist);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "origin_bearing", "f()", m3_origin_bearing);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Hardware presence
    result = m3_LinkRawFunction(module, "env", "gps_present", "i()", m3_gps_present);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "imu_present", "i()", m3_imu_present);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Battery percentage / runtime
    result = m3_LinkRawFunction(module, "env", "get_battery_percentage", "f()", m3_get_battery_percentage);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_battery_runtime", "f()", m3_get_battery_runtime);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Sun azimuth / elevation
    result = m3_LinkRawFunction(module, "env", "get_sun_azimuth", "f()", m3_get_sun_azimuth);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_sun_elevation", "f()", m3_get_sun_elevation);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
