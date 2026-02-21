#ifndef CONEZ_LORA_HAL_H
#define CONEZ_LORA_HAL_H

// RadioLib HAL for ESP32-S3 using raw GPSPI3 registers and ESP-IDF APIs.
// Adapted from RadioLib's NonArduino/ESP-IDF example for ESP32-S3.

#include <RadioLib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/spi_struct.h"
#include "soc/spi_reg.h"
#include "soc/gpio_sig_map.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"

// GPIO mode/level constants for RadioLibHal base class
#define LORA_HAL_INPUT   0x01
#define LORA_HAL_OUTPUT  0x03
#define LORA_HAL_LOW     0x00
#define LORA_HAL_HIGH    0x01
#define LORA_HAL_RISING  0x01
#define LORA_HAL_FALLING 0x02

#define LORA_HAL_DETACH_OUT  0x100
#define LORA_HAL_DETACH_IN   0x30

class EspHal : public RadioLibHal {
public:
    EspHal(int8_t sck, int8_t miso, int8_t mosi)
        : RadioLibHal(LORA_HAL_INPUT, LORA_HAL_OUTPUT,
                      LORA_HAL_LOW, LORA_HAL_HIGH,
                      LORA_HAL_RISING, LORA_HAL_FALLING),
          spiSCK(sck), spiMISO(miso), spiMOSI(mosi) {}

    void init() override {
        spiBegin();
    }

    void term() override {
        spiEnd();
    }

    // ---- GPIO ----

    void pinMode(uint32_t pin, uint32_t mode) override {
        if (pin == RADIOLIB_NC) return;
        gpio_config_t conf = {};
        conf.pin_bit_mask = (1ULL << pin);
        conf.mode = (gpio_mode_t)mode;
        conf.pull_up_en = GPIO_PULLUP_DISABLE;
        conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&conf);
    }

    void digitalWrite(uint32_t pin, uint32_t value) override {
        if (pin == RADIOLIB_NC) return;
        gpio_set_level((gpio_num_t)pin, value);
    }

    uint32_t digitalRead(uint32_t pin) override {
        if (pin == RADIOLIB_NC) return 0;
        return gpio_get_level((gpio_num_t)pin);
    }

    // ---- Interrupts ----

    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override {
        if (interruptNum == RADIOLIB_NC) return;
        gpio_install_isr_service((int)ESP_INTR_FLAG_IRAM);
        gpio_set_intr_type((gpio_num_t)interruptNum, (gpio_int_type_t)(mode & 0x7));
        gpio_isr_handler_add((gpio_num_t)interruptNum, (void (*)(void*))interruptCb, NULL);
    }

    void detachInterrupt(uint32_t interruptNum) override {
        if (interruptNum == RADIOLIB_NC) return;
        gpio_isr_handler_remove((gpio_num_t)interruptNum);
        gpio_set_intr_type((gpio_num_t)interruptNum, GPIO_INTR_DISABLE);
    }

    // ---- Timing ----

    void delay(RadioLibTime_t ms) override {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void delayMicroseconds(RadioLibTime_t us) override {
        uint64_t end = esp_timer_get_time() + us;
        while ((uint64_t)esp_timer_get_time() < end)
            asm volatile("nop");
    }

    RadioLibTime_t millis() override {
        return (RadioLibTime_t)(esp_timer_get_time() / 1000ULL);
    }

    RadioLibTime_t micros() override {
        return (RadioLibTime_t)(esp_timer_get_time());
    }

    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override {
        if (pin == RADIOLIB_NC) return 0;
        this->pinMode(pin, LORA_HAL_INPUT);
        uint32_t start = this->micros();
        uint32_t curtick = this->micros();
        while (this->digitalRead(pin) == state) {
            if ((this->micros() - curtick) > timeout)
                return 0;
        }
        return this->micros() - start;
    }

    // ---- SPI (GPSPI3 raw register access) ----

    void spiBegin() override {
        periph_module_enable(PERIPH_SPI3_MODULE);

        // ESP32-S3: enable SPI module clock gate and select PLL (80 MHz APB)
        GPSPI3.clk_gate.clk_en = 1;
        GPSPI3.clk_gate.mst_clk_active = 1;
        GPSPI3.clk_gate.mst_clk_sel = 1;

        // Reset control registers
        GPSPI3.slave.val = 0;
        GPSPI3.misc.val = 0;
        GPSPI3.user.val = 0;
        GPSPI3.user1.val = 0;
        GPSPI3.ctrl.val = 0;
        GPSPI3.clock.val = 0;
        for (int i = 0; i < 16; i++)
            GPSPI3.data_buf[i] = 0;

        // Full-duplex mode
        GPSPI3.user.usr_mosi = 1;
        GPSPI3.user.usr_miso = 1;
        GPSPI3.user.doutdin = 1;

        // SPI Mode 0: CPOL=0, CPHA=0
        GPSPI3.misc.ck_idle_edge = 0;
        GPSPI3.user.ck_out_edge = 0;

        // MSB first
        GPSPI3.ctrl.wr_bit_order = 0;
        GPSPI3.ctrl.rd_bit_order = 0;

        // 1 MHz clock — sufficient for SX126x LoRa radio
        GPSPI3.clock.val = spi3FreqToClkdiv(1000000);

        // Set data length for byte transfers (all RadioLib SPI is byte-by-byte)
        GPSPI3.ms_dlen.ms_data_bitlen = 7;

        // Synchronize register changes from APB domain into SPI module domain
        GPSPI3.cmd.update = 1;
        while (GPSPI3.cmd.update) ;

        // Route SPI3 signals through GPIO matrix
        this->pinMode(spiSCK, LORA_HAL_OUTPUT);
        this->pinMode(spiMISO, LORA_HAL_INPUT);
        this->pinMode(spiMOSI, LORA_HAL_OUTPUT);
        gpio_set_direction((gpio_num_t)spiSCK, GPIO_MODE_INPUT_OUTPUT);
        esp_rom_gpio_connect_out_signal(spiSCK, SPI3_CLK_OUT_IDX, false, false);
        esp_rom_gpio_connect_out_signal(spiMOSI, SPI3_D_OUT_IDX, false, false);
        esp_rom_gpio_connect_in_signal(spiMISO, SPI3_Q_IN_IDX, false);
    }

    void spiBeginTransaction() override {}

    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override {
        // ms_dlen already set to 7 (1 byte) and synced in spiBegin()
        for (size_t i = 0; i < len; i++) {
            GPSPI3.data_buf[0] = out[i];
            GPSPI3.cmd.usr = 1;
            while (GPSPI3.cmd.usr) ;
            in[i] = GPSPI3.data_buf[0] & 0xFF;
        }
    }

    void spiEndTransaction() override {}

    void spiEnd() override {
        esp_rom_gpio_connect_out_signal(spiSCK, LORA_HAL_DETACH_OUT, false, false);
        esp_rom_gpio_connect_in_signal(spiMISO, LORA_HAL_DETACH_IN, false);
        esp_rom_gpio_connect_out_signal(spiMOSI, LORA_HAL_DETACH_OUT, false, false);
    }

private:
    int8_t spiSCK;
    int8_t spiMISO;
    int8_t spiMOSI;

    // Compute SPI3 clock divider for ESP32-S3.
    // clkcnt_n: 6-bit, clkdiv_pre: 4-bit.
    static uint32_t spi3FreqToClkdiv(uint32_t freq) {
        const uint32_t apb = APB_CLK_FREQ;
        if (freq >= apb)
            return SPI_CLK_EQU_SYSCLK;

        uint32_t best_val = 0;
        uint32_t best_freq = 0;
        for (uint32_t n = 1; n <= 63; n++) {
            uint32_t pre = apb / (freq * (n + 1));
            if (pre > 0) pre--;
            for (uint32_t p = pre; p <= pre + 1 && p <= 15; p++) {
                uint32_t actual = apb / ((p + 1) * (n + 1));
                if (actual <= freq && actual > best_freq) {
                    best_freq = actual;
                    uint32_t h = ((n + 1) / 2) - 1;
                    best_val = (n & 0x3F)
                             | ((h & 0x3F) << 6)
                             | ((n & 0x3F) << 12)
                             | ((p & 0xF) << 18);
                    if (actual == freq) return best_val;
                }
            }
        }
        return best_val;
    }
};

#endif
