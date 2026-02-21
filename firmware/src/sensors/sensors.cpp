#include "sensors.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "adc.h"
#include "board.h"
#include "printManager.h"
#include "mpu6500.h"
#include "conez_usb.h"
#include "main.h"
#include <math.h>

#define TMP102_ADDR 0x48

static i2c_master_dev_handle_t tmp102_dev = NULL;

// TMP102 temperature read — inlined from abandoned yannicked/Sensor_TMP102 library.
// Datasheet: http://www.ti.com/lit/ds/symlink/tmp102.pdf
// Register 0x00 returns 12-bit signed temperature, 0.0625°C per LSB.
static float tmp102_read(void) {
    if (!tmp102_dev) return -500.0f;

    uint8_t reg = 0x00;
    uint8_t data[2];

    esp_err_t err = i2c_master_transmit_receive(tmp102_dev, &reg, 1, data, 2, pdMS_TO_TICKS(50));
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
    // Add TMP102 device to the I2C bus
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = TMP102_ADDR;
    dev_cfg.scl_speed_hz = 100000;
    i2c_master_bus_add_device(i2c_bus, &dev_cfg, &tmp102_dev);

    usb_printf("TMP102 initialized\n");

    if(!mpu6500_init())
    {
        usb_printf("MPU6500 does not respond\n");
    }
    else
    {
        usb_printf("MPU6500 is connected\n");
        IMU_available = true;
    }

    //Setup ADC's

    sensors_loop();

    usb_printf("TMP102 Temperature: %.2f C\n", temperature);
    usb_printf("MPU6500 Acceleration - X: %.2f, Y: %.2f, Z: %.2f\n", v_accX, v_accY, v_accZ);
    if (v_accZ < 0.5) {
        usb_printf("Looks like we are in space, not on earth...\n");
    } else {
        usb_printf("Seems we are on earth and upright, not in space...\n");
    }
    usb_printf("Batt: %.2f V   Solar: %.2f V\n", bat_voltage(), solar_voltage());
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
    adc_bat_mv = adc_read_mv(ADC_BAT_PIN);
#ifdef ADC_SOLAR_PIN
    adc_solar_mv = adc_read_mv(ADC_SOLAR_PIN);
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
