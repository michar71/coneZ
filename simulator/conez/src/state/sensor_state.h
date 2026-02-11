#ifndef SENSOR_STATE_H
#define SENSOR_STATE_H

#include <mutex>

struct SensorMock {
    // GPS
    float lat = 40.7860f;
    float lon = -119.2065f;
    float alt = 1190.0f;
    float speed = 0.0f;
    float dir = 0.0f;
    int gps_valid = 1;
    int gps_present = 1;

    // GPS origin/geometry
    float origin_lat = 40.7864f;
    float origin_lon = -119.2069f;
    int has_origin = 1;
    float origin_dist = 50.0f;
    float origin_bearing = 45.0f;

    // IMU
    float roll = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float acc_x = 0.0f;
    float acc_y = 0.0f;
    float acc_z = 1.0f;
    int imu_valid = 1;
    int imu_present = 1;

    // Environment
    float temp = 22.0f;
    float humidity = 30.0f;
    float brightness = 500.0f;

    // Power
    float bat_voltage = 12.6f;
    float solar_voltage = 14.0f;
    float battery_percentage = 85.0f;
    float battery_runtime = 480.0f;

    // Sun
    int sunrise = 360;     // 6:00 AM
    int sunset = 1140;     // 7:00 PM
    int sun_valid = 1;
    int is_daylight = 1;
    float sun_azimuth = 180.0f;
    float sun_elevation = 45.0f;

    // Cue
    int cue_playing = 0;
    int cue_elapsed = 0;
};

class SensorState {
public:
    SensorMock read() const;
    void write(const SensorMock &m);

    // Individual field setters for slider callbacks
    template<typename T>
    void set(T SensorMock::*field, T value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mock.*field = value;
    }

    template<typename T>
    T get(T SensorMock::*field) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_mock.*field;
    }

private:
    mutable std::mutex m_mutex;
    SensorMock m_mock;
};

SensorState &sensorState();

#endif
