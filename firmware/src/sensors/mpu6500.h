#ifndef MPU6500_H
#define MPU6500_H

#include <stdbool.h>

typedef struct { float x, y, z; } mpu_vec3_t;

bool       mpu6500_init(void);       // Reset, check WHO_AM_I, configure
bool       mpu6500_read(void);       // Read accel+gyro+temp; false = I2C read failed
void       mpu6500_calibrate(void);  // Average up to 50 samples, store offsets

mpu_vec3_t mpu6500_accel(void);      // Last accel in G
mpu_vec3_t mpu6500_gyro(void);       // Last gyro in deg/s
float      mpu6500_temp(void);       // Last temp in C

#endif
