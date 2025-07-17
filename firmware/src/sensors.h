#ifndef SENSORS_H
#define SENSORS_H   


void sensors_setup(void);
float getTemp(void);
float getAccX(void);
float getAccY(void);
float getAccZ(void);

float getPitch(void);
float getRoll(void);
float getYaw(void);

float getMaxAccXYZ(bool resetMax = false);
void sensors_loop(void);

#endif