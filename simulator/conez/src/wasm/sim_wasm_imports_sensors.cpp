#include "sim_wasm_imports.h"
#include "sensor_state.h"
#include "cue_engine.h"
#include "m3_env.h"

// Macro: float sensor returning a field
#define SENSOR_F(name, field) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(float); \
        m3ApiReturn(sensorState().get(&SensorMock::field)); \
    }

// Macro: int sensor returning a field
#define SENSOR_I(name, field) \
    m3ApiRawFunction(m3_##name) { \
        m3ApiReturnType(int32_t); \
        m3ApiReturn(sensorState().get(&SensorMock::field)); \
    }

// GPS
SENSOR_F(get_lat, lat)
SENSOR_F(get_lon, lon)
SENSOR_F(get_alt, alt)
SENSOR_F(get_speed, speed)
SENSOR_F(get_dir, dir)
SENSOR_I(gps_valid, gps_valid)
SENSOR_I(gps_present, gps_present)

// GPS origin
SENSOR_F(get_origin_lat, origin_lat)
SENSOR_F(get_origin_lon, origin_lon)
SENSOR_I(has_origin, has_origin)
SENSOR_F(origin_dist, origin_dist)
SENSOR_F(origin_bearing, origin_bearing)

// IMU
SENSOR_F(get_roll, roll)
SENSOR_F(get_pitch, pitch)
SENSOR_F(get_yaw, yaw)
SENSOR_F(get_acc_x, acc_x)
SENSOR_F(get_acc_y, acc_y)
SENSOR_F(get_acc_z, acc_z)
SENSOR_I(imu_valid, imu_valid)
SENSOR_I(imu_present, imu_present)

// Environment
SENSOR_F(get_temp, temp)
SENSOR_F(get_humidity, humidity)
SENSOR_F(get_brightness, brightness)

// Power
SENSOR_F(get_bat_voltage, bat_voltage)
SENSOR_F(get_solar_voltage, solar_voltage)
SENSOR_F(get_battery_percentage, battery_percentage)
SENSOR_F(get_battery_runtime, battery_runtime)

// Sun
SENSOR_I(get_sunrise, sunrise)
SENSOR_I(get_sunset, sunset)
SENSOR_I(sun_valid, sun_valid)
SENSOR_I(is_daylight, is_daylight)
SENSOR_F(get_sun_azimuth, sun_azimuth)
SENSOR_F(get_sun_elevation, sun_elevation)

// Cue — use engine when playing, fall back to sensor panel sliders
m3ApiRawFunction(m3_cue_playing) {
    m3ApiReturnType(int32_t);
    if (cueEngine().isPlaying())
        m3ApiReturn(1);
    m3ApiReturn(sensorState().get(&SensorMock::cue_playing));
}

m3ApiRawFunction(m3_cue_elapsed) {
    m3ApiReturnType(int64_t);
    if (cueEngine().isPlaying())
        m3ApiReturn(cueEngine().elapsedMs());
    m3ApiReturn((int64_t)sensorState().get(&SensorMock::cue_elapsed));
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_sensor_imports(IM3Module module)
{
    M3Result r;

    // GPS
    LINK("get_lat",    "f()", m3_get_lat)
    LINK("get_lon",    "f()", m3_get_lon)
    LINK("get_alt",    "f()", m3_get_alt)
    LINK("get_speed",  "f()", m3_get_speed)
    LINK("get_dir",    "f()", m3_get_dir)
    LINK("gps_valid",  "i()", m3_gps_valid)
    LINK("gps_present","i()", m3_gps_present)

    // GPS origin
    LINK("get_origin_lat",  "f()", m3_get_origin_lat)
    LINK("get_origin_lon",  "f()", m3_get_origin_lon)
    LINK("has_origin",      "i()", m3_has_origin)
    LINK("origin_dist",     "f()", m3_origin_dist)
    LINK("origin_bearing",  "f()", m3_origin_bearing)

    // IMU
    LINK("get_roll",   "f()", m3_get_roll)
    LINK("get_pitch",  "f()", m3_get_pitch)
    LINK("get_yaw",    "f()", m3_get_yaw)
    LINK("get_acc_x",  "f()", m3_get_acc_x)
    LINK("get_acc_y",  "f()", m3_get_acc_y)
    LINK("get_acc_z",  "f()", m3_get_acc_z)
    LINK("imu_valid",  "i()", m3_imu_valid)
    LINK("imu_present","i()", m3_imu_present)

    // Environment
    LINK("get_temp",       "f()", m3_get_temp)
    LINK("get_humidity",   "f()", m3_get_humidity)
    LINK("get_brightness", "f()", m3_get_brightness)

    // Power
    LINK("get_bat_voltage",        "f()", m3_get_bat_voltage)
    LINK("get_solar_voltage",      "f()", m3_get_solar_voltage)
    LINK("get_battery_percentage", "f()", m3_get_battery_percentage)
    LINK("get_battery_runtime",    "f()", m3_get_battery_runtime)

    // Sun
    LINK("get_sunrise",       "i()", m3_get_sunrise)
    LINK("get_sunset",        "i()", m3_get_sunset)
    LINK("sun_valid",         "i()", m3_sun_valid)
    LINK("is_daylight",       "i()", m3_is_daylight)
    LINK("get_sun_azimuth",   "f()", m3_get_sun_azimuth)
    LINK("get_sun_elevation", "f()", m3_get_sun_elevation)

    // Cue
    LINK("cue_playing", "i()", m3_cue_playing)
    LINK("cue_elapsed", "I()", m3_cue_elapsed)

    return m3Err_none;
}

#undef LINK
#undef SENSOR_F
#undef SENSOR_I
