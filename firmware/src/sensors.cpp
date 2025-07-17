#include "sensors.h"
#include <Arduino.h>
#include <Wire.h>
#include <Sensor_TMP102.h>
#include <MPU6500_WE.h>
#include "printManager.h"

#define MPU6500_ADDR 0x68
#define TMP102_ADDR 0x48
MPU6500_WE mpu(MPU6500_ADDR);
Sensor_TMP102 tmp;

float temperature = -500;
xyzFloat gValue ;
xyzFloat gyr;
xyzFloat angle;
float mpu_temp;
float resultantG = 0;
int adc_bat_mv = 0;
int adc_solar_mv = 0;


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
    Serial.printf("Bat Voltage: %.2f V Solar Voltage: %.2f V\n", bat_voltage(), solar_voltage());
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
    //printfnl(SOURCE_SENSORS, "MPU6500 Acceleration - X: %.2f, Y: %.2f, Z: %.2f", accX, accY, accZ);

    //Read ADC's
    adc_bat_mv = analogReadMilliVolts(1); 
    adc_solar_mv = analogReadMilliVolts(2); 
}



float getTemp(void)
{
    return temperature;
}

float getAccX(void)
{
    return gValue.x;
}

float getAccY(void)
{
    return gValue.y;
}

float getAccZ(void)
{
    return gValue.z;
}

float getPitch(void)
{
    return atan2(gValue.y, sqrt(gValue.x * gValue.x + gValue.z * gValue.z)) * 180.0 / M_PI;
}

float getRoll(void)
{
    return atan2(-gValue.x, gValue.z) * 180.0 / M_PI;
}

float getYaw(void)
{
    return atan2(gValue.y, gValue.x) * 180.0 / M_PI;
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
