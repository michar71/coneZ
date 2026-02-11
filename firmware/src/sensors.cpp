#include "sensors.h"
#include <Arduino.h>
#include <Wire.h>
#include <Sensor_TMP102.h>
#include <MPU6500_WE.h>
#include "board.h"
#include "printManager.h"

#define MPU6500_ADDR 0x68
#define TMP102_ADDR 0x48
MPU6500_WE mpu(MPU6500_ADDR);
Sensor_TMP102 tmp;

// Marked volatile for cross-core visibility (Core 1 writes, Core 0 reads).
volatile float temperature = -500;
xyzFloat gValue;
xyzFloat gyr;
xyzFloat angle;
volatile float mpu_temp;
volatile float resultantG = 0;

// Cached volatile copies of IMU data for cross-core reads
volatile float v_accX = 0, v_accY = 0, v_accZ = 0;
volatile float v_pitch = 0, v_roll = 0, v_yaw = 0;
volatile int adc_bat_mv = 0;
volatile int adc_solar_mv = 0;
bool IMU_avaliable = false;


void sensors_setup(void)
{
    tmp.begin(TMP102_ADDR);
    Serial.println("TMP102 initalized");

    if(!mpu.init())
    {
        Serial.println("MPU6500 does not respond");
    }
    else
    {
        Serial.println("MPU6500 is connected");
        mpu.enableGyrDLPF();
        mpu.setGyrDLPF(MPU6500_DLPF_6);
        mpu.setSampleRateDivider(5);
        mpu.setGyrRange(MPU6500_GYRO_RANGE_250);
        mpu.setAccRange(MPU6500_ACC_RANGE_2G);
        mpu.enableAccDLPF(true);
        mpu.setAccDLPF(MPU6500_DLPF_6);
        IMU_avaliable = true;
    }

    //Setup ADC's
    
    sensors_loop();

    Serial.printf("TMP102 Temperature: %.2f C\n", temperature);
    Serial.printf("MPU6500 Acceleration - X: %.2f, Y: %.2f, Z: %.2f\n", gValue.x, gValue.y, gValue.z);
    if (gValue.z < 0.5) {
        Serial.println("Looks like we are in space, not on earth...");
    } else {
        Serial.println("Seems we are on earth and upright, not in space...");
    }
    Serial.printf("Batt: %.2f V   Solar: %.2f V\n", bat_voltage(), solar_voltage());
}

void sensors_loop(void)
{
    // Read temperature from TMP102
    temperature = tmp.readTemperature();
    //printfnl(SOURCE_SENSORS, "TMP102 Temperature: %.2f C", temperature);

    // Read accelerometer data from MPU6500
    gValue = mpu.getGValues();
    gyr = mpu.getGyrValues();
    mpu_temp = mpu.getTemperature();
    angle = mpu.getAngles();
    resultantG = mpu.getResultantG(gValue);

    // Cache volatile copies for cross-core reads
    v_accX = gValue.x;
    v_accY = gValue.y;
    v_accZ = gValue.z;
    //printfnl(SOURCE_SENSORS, "MPU6500 Acceleration - X: %.2f, Y: %.2f, Z: %.2f", accX, accY, accZ);

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

bool imuAvaialble(void)
{
    return IMU_avaliable;
}


bool MPU_Calibrate(void)
{
    mpu.autoOffsets();
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

    if (accX > maxAcc) {
        maxAcc = accX;
    }
    if (accY > maxAcc) {
        maxAcc = accY;
    }
    if (accZ > maxAcc) {
        maxAcc = accZ;
    }

    return maxAcc;
}
    // Placeholder for temperature reading logic

float bat_voltage(void)
{
    static int last_val = 0;

    int newval = (last_val + adc_bat_mv)/2;
    last_val = adc_bat_mv;
    return (newval / 1000.0); // Convert millivolts to volts
}

float solar_voltage(void)
{
    static int last_val = 0;

    int newval = (last_val + adc_solar_mv)/2;
    last_val = adc_solar_mv;
    return (newval / 1000.0); // Convert millivolts to volts
}
