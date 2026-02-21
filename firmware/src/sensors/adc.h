// ESP-IDF ADC wrapper — replaces Arduino analogRead/analogReadMilliVolts.
// Both boards are ESP32-S3; ADC1 channels 0-9 map to GPIO 1-10.

#ifndef CONEZ_ADC_H
#define CONEZ_ADC_H

// Initialize ADC1 with 12-bit width + calibration.
// Call once from setup() after config_init().
void adc_setup(void);

// Read calibrated millivolts from a GPIO pin (1-10 on ESP32-S3).
// Returns 0 if gpio is out of range or ADC not initialized.
int adc_read_mv(int gpio);

// Read raw 12-bit ADC value from a GPIO pin (1-10 on ESP32-S3).
// Returns 0 if gpio is out of range or ADC not initialized.
int adc_read_raw(int gpio);

#endif
