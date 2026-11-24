#ifndef _conez_util_h
#define _conez_util_h

#include <stdint.h>

// Blink an error code on status LED forever.
void blinkloop( int flashes );

// Dump buffer as hex values to output stream.
void hexdump( uint8_t *buf, int len );

// GPIO pin guards, shared by the `gpio` CLI command and the WASM pin_* imports.
// gpio_valid_pin(): a real ESP32-S3 GPIO (0-21, 33-48; 22-32 are flash/unbonded).
// gpio_is_reserved(): wired to critical board hardware (USB, PSRAM/LoRa SPI, GPS,
// I2C) -- reconfiguring it kills that peripheral until reboot.
bool gpio_valid_pin(int pin);
bool gpio_is_reserved(int pin);

#endif