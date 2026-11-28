#ifndef FWHOST_ESP_RANDOM_H
#define FWHOST_ESP_RANDOM_H
#include <stdint.h>
// Deterministic stand-in for the hardware RNG (same sequence as the Node
// harness, so RANDOM() tests have one expected output on every host).
uint32_t esp_random(void);
#endif
