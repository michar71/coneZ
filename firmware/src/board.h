#ifndef _CONEZ_BOARD_H
#define _CONEZ_BOARD_H

// Board-specific configuration stuff.
#ifdef BOARD_CONEZ_V0_1         // Custom ConeZ v0.1 PCB
    // CPU
    #define BOARD_CPU_ESP32S3

    // Memory
    #define BOARD_SPIFLASH_SIZE     (8*1024*1024)
    #define BOARD_HAS_IMPROVISED_PSRAM                  // PSRAM hanging off aux SPI interface
    #define BOARD_PSRAM_SIZE        (2*1024*1024)

    // LoRa radio
    #define BOARD_HAS_LORA
    #define BOARD_LORA_SX1268
    #define LORA_PIN_CS     8
    #define LORA_PIN_DIO1   14
    #define LORA_PIN_RST    12
    #define LORA_PIN_BUSY   13
    #define LORA_PIN_NSS    8
    #define LORA_PIN_SCK    9
    #define LORA_PIN_MOSI   10
    #define LORA_PIN_MISO   11

    // GPS
    #define BOARD_HAS_GPS
    #define GPS_RX_PIN      40
    #define GPS_TX_PIN      39
    #define GPS_PPS_PIN     42

    // Audio-Buzzer
    #define BOARD_HAS_BUZZER
    #define BUZZER_PIN      48

#elif DEFINED( BOARD_HELTEC_LORA32_V3 )     // Heltec LoRa32 v3
    // CPU
    #define BOARD_CPU_ESP32S3

    // Memory
    #define BOARD_SPIFLASH_SIZE     (8*1024*1024)
    #define BOARD_HAS_PSRAM                         // Natively connected PSRAM
    #define BOARD_PSRAM_SIZE        (8*1024*1024)

    // LoRa radio
    #define BOARD_HAS_LORA
    #define BOARD_LORA_SX1262
    #define LORA_PIN_CS     8
    #define LORA_PIN_DIO1   14
    #define LORA_PIN_RST    12
    #define LORA_PIN_BUSY   13
    #define LORA_PIN_NSS    8
    #define LORA_PIN_SCK    9
    #define LORA_PIN_MOSI   10
    #define LORA_PIN_MISO   11

    // GPS

#else   // Default unspecified board

#endif

#endif