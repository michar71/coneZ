#ifndef main_h
#define main_h

#include "FastLED.h"

// Misc GPIO pins
#define LED_PIN 40


// I2C pins
#define I2C_SDA_PIN   17
#define I2C_SCL_PIN   18


// Pin mapping for SX1262 on Heltec LoRa32 v3
#define LORA_PIN_CS     8
#define LORA_PIN_DIO1   14
#define LORA_PIN_RST    12
#define LORA_PIN_BUSY   13

#define LORA_NSS        8
#define LORA_SCK        9
#define LORA_MOSI       10
#define LORA_MISO       11

//PSRAM
#define PSR_CE          4
#define PSR_MISO        5
#define PSR_SCK         6
#define PSR_MOSI        7

//RGB Strin Pins
#define RGB1_PIN        38
#define RGB2_PIN        37
#define RGB3_PIN        36
#define RGB4_PIN        35

//Hartmann extension connector
#define EXT1_PIN        15
#define EXT2_PIN        16

//Power Managhment/Charging
#define ADC_BAT_PIN     1
#define ADC_SOLAR_PIN   2
#define SOLAR_PWM_PIN   21
#define LOAD_ON_PIN     47
#define PWR_SW_PIN      33
#define PWR_OFF_PIN     34

//Audio-Buzzer
#define BUZZER_PIN     48

//GPS
#define GPS_RX_PIN      40
#define GPS_TX_PIN      39
#define GPS_PPS_PIN     42

//Other
#define IMU_INT_PIN     37


#define NUM_LEDS1 32
#define NUM_LEDS2 32
#define NUM_LEDS3 32
#define NUM_LEDS4 32

CRGB leds1[NUM_LEDS1];
CRGB leds2[NUM_LEDS2];
CRGB leds3[NUM_LEDS3];
CRGB leds4[NUM_LEDS4];

#endif