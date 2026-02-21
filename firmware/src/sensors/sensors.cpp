#include "sensors.h"
#include <Arduino.h>
#include "driver/i2c.h"
#include "board.h"
#include "printManager.h"
#include "mpu6500.h"
#include <math.h>

#define TMP102_ADDR 0x48

// TMP102 temperature read — inlined from abandoned yannicked/Sensor_TMP102 library.
// Datasheet: http://www.ti.com/lit/ds/symlink/tmp102.pdf
// Register 0x00 returns 12-bit signed temperature, 0.0625°C per LSB.
static float tmp102_read(void) {
    uint8_t reg = 0x00;
    uint8_t data[2];

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TMP102_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);  // repeated start
    i2c_master_write_byte(cmd, (TMP102_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if (err != ESP_OK) return -500.0f;

    int16_t raw = (data[0] << 4) | (data[1] >> 4);
    if (raw > 0x7FF) raw |= 0xF000;  // sign-extend 12-bit
    return raw * 0.0625f;
}

// Marked volatile for cross-core visibility (Core 1 writes, Core 0 reads).
volatile float temperature = -500;
volatile float mpu_temp;
volatile float resultantG = 0;

// Cached volatile copies of IMU data for cross-core reads
volatile float v_accX = 0, v_accY = 0, v_accZ = 0;
volatile float v_pitch = 0, v_roll = 0, v_yaw = 0;
volatile int adc_bat_mv = 0;
volatile int adc_solar_mv = 0;
bool IMU_available = false;


void sensors_setup(void)
{
    Serial.println("TMP102 initialized");

    if(!mpu6500_init())
    {
        Serial.println("MPU6500 does not respond");
    }
    else
    {
        Serial.println("MPU6500 is connected");
        IMU_available = true;
    }

    //Setup ADC's

    sensors_loop();

    Serial.printf("TMP102 Temperature: %.2f C\n", temperature);
    Serial.printf("MPU6500 Acceleration - X: %.2f, Y: %.2f, Z: %.2f\n", v_accX, v_accY, v_accZ);
    if (v_accZ < 0.5) {
        Serial.println("Looks like we are in space, not on earth...");
    } else {
        Serial.println("Seems we are on earth and upright, not in space...");
    }
    Serial.printf("Batt: %.2f V   Solar: %.2f V\n", bat_voltage(), solar_voltage());
}

void sensors_loop(void)
{
    // Read temperature from TMP102
    temperature = tmp102_read();

    // Read accelerometer data from MPU6500
    if (IMU_available) {
        mpu6500_read();
        mpu_vec3_t acc = mpu6500_accel();
        mpu_temp = mpu6500_temp();
        resultantG = sqrtf(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);

        // Cache volatile copies for cross-core reads
        v_accX = acc.x;
        v_accY = acc.y;
        v_accZ = acc.z;
    }

    //Read ADC's
    adc_bat_mv = analogReadMilliVolts(ADC_BAT_PIN);
#ifdef ADC_SOLAR_PIN
    adc_solar_mv = analogReadMilliVolts(ADC_SOLAR_PIN);
#endif
}



float getTemp(void)
{
    return temperature;
}

float getAccX(void)
{
    return v_accX;
}

float getAccY(void)
{
    return v_accY;
}

float getAccZ(void)
{
    return v_accZ;
}

float getPitch(void)
{
    float ax = v_accX, ay = v_accY, az = v_accZ;
    return atan2(ay, sqrt(ax * ax + az * az)) * 180.0 / M_PI;
}

float getRoll(void)
{
    float ax = v_accX, az = v_accZ;
    return atan2(-ax, az) * 180.0 / M_PI;
}

float getYaw(void)
{
    float ax = v_accX, ay = v_accY;
    return atan2(ay, ax) * 180.0 / M_PI;
}

bool imuAvailable(void)
{
    return IMU_available;
}


bool MPU_Calibrate(void)
{
    mpu6500_calibrate();
    printfnl(SOURCE_SENSORS,"MPU6500 calibration done");
    return true;
}


float getMaxAccXYZ(bool resetMax)
{
    static float maxAcc = 0;
    float accX = getAccX();
    float accY = getAccY();
    float accZ = getAccZ();

    if (resetMax) {
        maxAcc = 0;
    }

    if (fabs(accX) > maxAcc) {
        maxAcc = fabs(accX);
    }
    if (fabs(accY) > maxAcc) {
        maxAcc = fabs(accY);
    }
    if (fabs(accZ) > maxAcc) {
        maxAcc = fabs(accZ);
    }

    return maxAcc;
}

float bat_voltage(void)
{
    static int last_val = -1;

    if (last_val < 0) last_val = adc_bat_mv;
    int newval = (last_val + adc_bat_mv)/2;
    last_val = adc_bat_mv;
    return (newval / 1000.0); // Convert millivolts to volts
}

float solar_voltage(void)
{
    static int last_val = -1;

    if (last_val < 0) last_val = adc_solar_mv;
    int newval = (last_val + adc_solar_mv)/2;
    last_val = adc_solar_mv;
    return (newval / 1000.0); // Convert millivolts to volts
}
