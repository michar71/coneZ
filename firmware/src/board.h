#ifndef _CONEZ_BOARD_H
#define _CONEZ_BOARD_H

// Board-specific configuration stuff.
#ifdef BOARD_CONEZ_V0_1         // Custom ConeZ v0.1 PCB
    // CPU
    #define BOARD_CPU_ESP32S3

    // Feature flags
    #define BOARD_HAS_LORA
    #define BOARD_HAS_GPS
    #define BOARD_HAS_BUZZER
    #define BOARD_HAS_RGB_LEDS
    #define BOARD_HAS_POWER_MGMT
    #define BOARD_HAS_IMU

    // Memory
    #define BOARD_SPIFLASH_SIZE     (8*1024*1024)
    #define BOARD_HAS_IMPROVISED_PSRAM                  // PSRAM hanging off aux SPI interface
    #define BOARD_PSRAM_SIZE        (8*1024*1024)

    // Status LED
    #define LED_PIN         40

    // I2C
    #define I2C_SDA_PIN     17
    #define I2C_SCL_PIN     18

    // PSRAM SPI
    #define PSR_CE          5
    #define PSR_MISO        4
    #define PSR_SCK         6
    #define PSR_MOSI        7

    // RGB LED Strips
    #define RGB1_PIN        38
    #define RGB2_PIN        37
    #define RGB3_PIN        36
    #define RGB4_PIN        35

    // Hartmann extension connector
    #define EXT1_PIN        15
    #define EXT2_PIN        16

    // Power Management
    #define ADC_BAT_PIN     1
    #define ADC_SOLAR_PIN   2
    #define SOLAR_PWM_PIN   21
    #define LOAD_ON_PIN     47
    #define PWR_SW_PIN      33
    #define PWR_OFF_PIN     34

    // IMU
    #define IMU_INT_PIN     41

    // LoRa radio
    #define BOARD_LORA_SX1268
    #define LORA_PIN_CS     8
    #define LORA_PIN_DIO1   14
    #define LORA_PIN_RST    12
    #define LORA_PIN_BUSY   13

    #define LORA_PIN_SCK    9
    #define LORA_PIN_MOSI   10
    #define LORA_PIN_MISO   11

    // GPS (default UART0 pins)
    #define GPS_RX_PIN      44
    #define GPS_TX_PIN      43
    #define GPS_PPS_PIN     42

    // Audio-Buzzer
    #define BUZZER_PIN      48

#elif defined( BOARD_HELTEC_LORA32_V3 )     // Heltec LoRa32 v3
    // CPU
    #define BOARD_CPU_ESP32S3

    // Feature flags
    #define BOARD_HAS_LORA
    #define BOARD_HAS_OLED

    // Memory
    #define BOARD_SPIFLASH_SIZE     (8*1024*1024)

    // Status LED
    #define LED_PIN         35

    // I2C (shared with OLED)
    #define I2C_SDA_PIN     17
    #define I2C_SCL_PIN     18

    // OLED
    #define OLED_RST_PIN    21

    // Battery ADC
    #define ADC_BAT_PIN     1

    // Vext power control
    #define VEXT_PIN        36

    // Button
    #define BUTTON_PIN      0

    // LoRa radio
    #define BOARD_LORA_SX1262
    #define LORA_PIN_CS     8
    #define LORA_PIN_DIO1   14
    #define LORA_PIN_RST    12
    #define LORA_PIN_BUSY   13

    #define LORA_PIN_SCK    9
    #define LORA_PIN_MOSI   10
    #define LORA_PIN_MISO   11

#else   // Default unspecified board

#endif

#endif
