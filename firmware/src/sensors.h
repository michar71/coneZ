#ifndef SENSORS_H
#define SENSORS_H   


void sensors_setup(void);
bool imuAvailable(void);
float getTemp(void);
float getAccX(void);
float getAccY(void);
float getAccZ(void);

float getPitch(void);
float getRoll(void);
float getYaw(void);

float bat_voltage(void);
float solar_voltage(void);

float getMaxAccXYZ(bool resetMax = false);
void sensors_loop(void);

#endif