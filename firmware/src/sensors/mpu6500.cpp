#include "mpu6500.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define MPU6500_ADDR      0x68

// Registers
#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A
#define REG_GYRO_CONFIG   0x1B
#define REG_ACCEL_CONFIG  0x1C
#define REG_ACCEL_CONFIG2 0x1D
#define REG_ACCEL_XOUT_H  0x3B
#define REG_PWR_MGMT_1    0x6B
#define REG_WHO_AM_I      0x75

// Scaling for 2G accel range, 250 deg/s gyro range
#define ACCEL_SCALE       (1.0f / 16384.0f)
#define GYRO_SCALE        (250.0f / 32768.0f)

// Raw offset storage (set by calibrate)
static int16_t off_ax, off_ay, off_az;
static int16_t off_gx, off_gy, off_gz;

// Last-read scaled values
static mpu_vec3_t last_accel;
static mpu_vec3_t last_gyro;
static float      last_temp;

// ---- I2C helpers (same pattern as tmp102_read in sensors.cpp) ----

static esp_err_t mpu_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t mpu_read_regs(uint8_t reg, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);  // repeated start
    i2c_master_write_byte(cmd, (MPU6500_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, buf, len, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    return err;
}

// ---- Public API ----

bool mpu6500_init(void)
{
    // Reset device
    if (mpu_write_reg(REG_PWR_MGMT_1, 0x80) != ESP_OK)
        return false;
    vTaskDelay(pdMS_TO_TICKS(100));

    // Wake up, select best clock source (PLL with gyro X)
    mpu_write_reg(REG_PWR_MGMT_1, 0x01);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Check WHO_AM_I (MPU6500 = 0x70, MPU9250 = 0x71)
    uint8_t who = 0;
    mpu_read_regs(REG_WHO_AM_I, &who, 1);
    if (who != 0x70 && who != 0x71)
        return false;

    // Sample rate divider = 5 → 1kHz/(1+5) = 166 Hz
    mpu_write_reg(REG_SMPLRT_DIV, 5);

    // Gyro config: DLPF mode (FCHOICE_B=0), DLPF=6 (5 Hz bandwidth)
    mpu_write_reg(REG_CONFIG, 6);
    // Gyro range: 250 deg/s (bits 4:3 = 00), FCHOICE_B = 00
    mpu_write_reg(REG_GYRO_CONFIG, 0x00);

    // Accel range: 2G (bits 4:3 = 00)
    mpu_write_reg(REG_ACCEL_CONFIG, 0x00);
    // Accel DLPF enable + DLPF=6 (5 Hz bandwidth)
    mpu_write_reg(REG_ACCEL_CONFIG2, 0x06);

    // Clear offsets
    off_ax = off_ay = off_az = 0;
    off_gx = off_gy = off_gz = 0;

    return true;
}

void mpu6500_read(void)
{
    // Burst-read 14 bytes: accel(6) + temp(2) + gyro(6) from 0x3B
    uint8_t buf[14];
    if (mpu_read_regs(REG_ACCEL_XOUT_H, buf, 14) != ESP_OK)
        return;

    int16_t raw_ax = (int16_t)((buf[0]  << 8) | buf[1]);
    int16_t raw_ay = (int16_t)((buf[2]  << 8) | buf[3]);
    int16_t raw_az = (int16_t)((buf[4]  << 8) | buf[5]);
    int16_t raw_t  = (int16_t)((buf[6]  << 8) | buf[7]);
    int16_t raw_gx = (int16_t)((buf[8]  << 8) | buf[9]);
    int16_t raw_gy = (int16_t)((buf[10] << 8) | buf[11]);
    int16_t raw_gz = (int16_t)((buf[12] << 8) | buf[13]);

    // Apply calibration offsets
    raw_ax -= off_ax;
    raw_ay -= off_ay;
    raw_az -= off_az;
    raw_gx -= off_gx;
    raw_gy -= off_gy;
    raw_gz -= off_gz;

    last_accel.x = raw_ax * ACCEL_SCALE;
    last_accel.y = raw_ay * ACCEL_SCALE;
    last_accel.z = raw_az * ACCEL_SCALE;

    last_gyro.x = raw_gx * GYRO_SCALE;
    last_gyro.y = raw_gy * GYRO_SCALE;
    last_gyro.z = raw_gz * GYRO_SCALE;

    last_temp = raw_t / 333.87f + 21.0f;
}

void mpu6500_calibrate(void)
{
    int32_t sum_ax = 0, sum_ay = 0, sum_az = 0;
    int32_t sum_gx = 0, sum_gy = 0, sum_gz = 0;

    // Clear offsets so raw reads are unbiased
    off_ax = off_ay = off_az = 0;
    off_gx = off_gy = off_gz = 0;

    const int N = 50;
    for (int i = 0; i < N; i++) {
        uint8_t buf[14];
        if (mpu_read_regs(REG_ACCEL_XOUT_H, buf, 14) != ESP_OK)
            continue;

        sum_ax += (int16_t)((buf[0]  << 8) | buf[1]);
        sum_ay += (int16_t)((buf[2]  << 8) | buf[3]);
        sum_az += (int16_t)((buf[4]  << 8) | buf[5]);
        sum_gx += (int16_t)((buf[8]  << 8) | buf[9]);
        sum_gy += (int16_t)((buf[10] << 8) | buf[11]);
        sum_gz += (int16_t)((buf[12] << 8) | buf[13]);

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    off_ax = (int16_t)(sum_ax / N);
    off_ay = (int16_t)(sum_ay / N);
    off_az = (int16_t)(sum_az / N - 16384);  // Subtract 1G on Z axis
    off_gx = (int16_t)(sum_gx / N);
    off_gy = (int16_t)(sum_gy / N);
    off_gz = (int16_t)(sum_gz / N);
}

mpu_vec3_t mpu6500_accel(void) { return last_accel; }
mpu_vec3_t mpu6500_gyro(void)  { return last_gyro; }
float      mpu6500_temp(void)  { return last_temp; }
