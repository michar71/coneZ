#include "sensor_panel.h"
#include "sensor_state.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

SensorPanel::SensorPanel(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setMinimumWidth(260);
    setMaximumWidth(350);

    auto *container = new QWidget;
    auto *mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // GPS group
    auto *gps = new QGroupBox("GPS");
    auto *gpsL = new QVBoxLayout(gps);
    addFloatSlider(gpsL, "Latitude",  -90, 90, 40.786f, 4,
        [](float v) { sensorState().set(&SensorMock::lat, v); });
    addFloatSlider(gpsL, "Longitude", -180, 180, -119.2065f, 4,
        [](float v) { sensorState().set(&SensorMock::lon, v); });
    addFloatSlider(gpsL, "Altitude", 0, 5000, 1190, 0,
        [](float v) { sensorState().set(&SensorMock::alt, v); });
    addFloatSlider(gpsL, "Speed", 0, 50, 0, 1,
        [](float v) { sensorState().set(&SensorMock::speed, v); });
    addFloatSlider(gpsL, "Direction", 0, 360, 0, 0,
        [](float v) { sensorState().set(&SensorMock::dir, v); });
    addIntSlider(gpsL, "GPS Valid", 0, 1, 1,
        [](int v) { sensorState().set(&SensorMock::gps_valid, v); });
    mainLayout->addWidget(gps);

    // Origin group
    auto *orig = new QGroupBox("Origin");
    auto *origL = new QVBoxLayout(orig);
    addFloatSlider(origL, "Distance (m)", 0, 1000, 50, 0,
        [](float v) { sensorState().set(&SensorMock::origin_dist, v); });
    addFloatSlider(origL, "Bearing", 0, 360, 45, 0,
        [](float v) { sensorState().set(&SensorMock::origin_bearing, v); });
    addIntSlider(origL, "Has Origin", 0, 1, 1,
        [](int v) { sensorState().set(&SensorMock::has_origin, v); });
    mainLayout->addWidget(orig);

    // IMU group
    auto *imu = new QGroupBox("IMU");
    auto *imuL = new QVBoxLayout(imu);
    addFloatSlider(imuL, "Roll",  -180, 180, 0, 1,
        [](float v) { sensorState().set(&SensorMock::roll, v); });
    addFloatSlider(imuL, "Pitch", -180, 180, 0, 1,
        [](float v) { sensorState().set(&SensorMock::pitch, v); });
    addFloatSlider(imuL, "Yaw",   -180, 180, 0, 1,
        [](float v) { sensorState().set(&SensorMock::yaw, v); });
    addFloatSlider(imuL, "Acc X", -16, 16, 0, 2,
        [](float v) { sensorState().set(&SensorMock::acc_x, v); });
    addFloatSlider(imuL, "Acc Y", -16, 16, 0, 2,
        [](float v) { sensorState().set(&SensorMock::acc_y, v); });
    addFloatSlider(imuL, "Acc Z", -16, 16, 1.0f, 2,
        [](float v) { sensorState().set(&SensorMock::acc_z, v); });
    addIntSlider(imuL, "IMU Valid", 0, 1, 1,
        [](int v) { sensorState().set(&SensorMock::imu_valid, v); });
    mainLayout->addWidget(imu);

    // Environment group
    auto *env = new QGroupBox("Environment");
    auto *envL = new QVBoxLayout(env);
    addFloatSlider(envL, "Temp (C)", -20, 60, 22, 1,
        [](float v) { sensorState().set(&SensorMock::temp, v); });
    addFloatSlider(envL, "Humidity (%)", 0, 100, 30, 0,
        [](float v) { sensorState().set(&SensorMock::humidity, v); });
    addFloatSlider(envL, "Brightness", 0, 4096, 500, 0,
        [](float v) { sensorState().set(&SensorMock::brightness, v); });
    mainLayout->addWidget(env);

    // Power group
    auto *pwr = new QGroupBox("Power");
    auto *pwrL = new QVBoxLayout(pwr);
    addFloatSlider(pwrL, "Battery V", 0, 15, 12.6f, 1,
        [](float v) { sensorState().set(&SensorMock::bat_voltage, v); });
    addFloatSlider(pwrL, "Solar V", 0, 20, 14, 1,
        [](float v) { sensorState().set(&SensorMock::solar_voltage, v); });
    addFloatSlider(pwrL, "Battery %", 0, 100, 85, 0,
        [](float v) { sensorState().set(&SensorMock::battery_percentage, v); });
    addFloatSlider(pwrL, "Runtime (min)", 0, 1440, 480, 0,
        [](float v) { sensorState().set(&SensorMock::battery_runtime, v); });
    mainLayout->addWidget(pwr);

    // Sun group
    auto *sun = new QGroupBox("Sun");
    auto *sunL = new QVBoxLayout(sun);
    addIntSlider(sunL, "Sunrise (min)", 0, 1440, 360,
        [](int v) { sensorState().set(&SensorMock::sunrise, v); });
    addIntSlider(sunL, "Sunset (min)", 0, 1440, 1140,
        [](int v) { sensorState().set(&SensorMock::sunset, v); });
    addIntSlider(sunL, "Is Daylight", -1, 1, 1,
        [](int v) { sensorState().set(&SensorMock::is_daylight, v); });
    addFloatSlider(sunL, "Azimuth", 0, 360, 180, 0,
        [](float v) { sensorState().set(&SensorMock::sun_azimuth, v); });
    addFloatSlider(sunL, "Elevation", -90, 90, 45, 0,
        [](float v) { sensorState().set(&SensorMock::sun_elevation, v); });
    mainLayout->addWidget(sun);

    // Cue group
    auto *cue = new QGroupBox("Cue");
    auto *cueL = new QVBoxLayout(cue);
    addIntSlider(cueL, "Playing", 0, 1, 0,
        [](int v) { sensorState().set(&SensorMock::cue_playing, v); });
    addIntSlider(cueL, "Elapsed (ms)", 0, 600000, 0,
        [](int v) { sensorState().set(&SensorMock::cue_elapsed, v); });
    mainLayout->addWidget(cue);

    mainLayout->addStretch();
    setWidget(container);
}

void SensorPanel::addFloatSlider(QVBoxLayout *layout, const QString &label,
                                 float min, float max, float def, int decimals,
                                 std::function<void(float)> setter)
{
    auto *row = new QWidget;
    auto *rl = new QVBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(1);

    auto *spin = new QDoubleSpinBox;
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setValue(def);
    spin->setSingleStep(decimals >= 2 ? 0.01 : (decimals >= 1 ? 0.1 : 1.0));
    spin->setPrefix(label + ": ");
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *slider = new QSlider(Qt::Horizontal);
    int steps = 1000;
    slider->setRange(0, steps);
    slider->setValue((int)((def - min) / (max - min) * steps));

    // Slider -> spin
    QObject::connect(slider, &QSlider::valueChanged, [=](int v) {
        float fv = min + (max - min) * v / steps;
        spin->blockSignals(true);
        spin->setValue(fv);
        spin->blockSignals(false);
        setter(fv);
    });

    // Spin -> slider
    QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [=](double v) {
        slider->blockSignals(true);
        slider->setValue((int)((v - min) / (max - min) * steps));
        slider->blockSignals(false);
        setter((float)v);
    });

    rl->addWidget(spin);
    rl->addWidget(slider);
    layout->addWidget(row);
}

void SensorPanel::addIntSlider(QVBoxLayout *layout, const QString &label,
                               int min, int max, int def,
                               std::function<void(int)> setter)
{
    auto *row = new QWidget;
    auto *rl = new QVBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(1);

    auto *spin = new QSpinBox;
    spin->setRange(min, max);
    spin->setValue(def);
    spin->setPrefix(label + ": ");
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(min, max);
    slider->setValue(def);

    QObject::connect(slider, &QSlider::valueChanged, [=](int v) {
        spin->blockSignals(true);
        spin->setValue(v);
        spin->blockSignals(false);
        setter(v);
    });

    QObject::connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), [=](int v) {
        slider->blockSignals(true);
        slider->setValue(v);
        slider->blockSignals(false);
        setter(v);
    });

    rl->addWidget(spin);
    rl->addWidget(slider);
    layout->addWidget(row);
}
