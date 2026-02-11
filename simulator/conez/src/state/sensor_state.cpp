#include "sensor_state.h"

static SensorState s_sensors;
SensorState &sensorState() { return s_sensors; }

SensorMock SensorState::read() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_mock;
}

void SensorState::write(const SensorMock &m)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_mock = m;
}
