#include "adc.h"
#include "board.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"

static esp_adc_cal_characteristics_t adc_chars;
static bool adc_initialized = false;
static bool adc_chan_configured[10] = {};

void adc_setup(void)
{
    // 12-bit resolution (0-4095)
    adc1_config_width(ADC_WIDTH_BIT_12);

    // Configure channels 0-2 (GPIO 1-3) — safe for ADC, no SPI overlap.
    // GPIO 4+ overlaps with PSRAM SPI (4-7) and LoRa SPI (8-10), so
    // configuring those as ADC would switch the pin to analog and break SPI.
    for (int ch = 0; ch < 3; ch++) {
        adc1_config_channel_atten((adc1_channel_t)ch, ADC_ATTEN_DB_11);
        adc_chan_configured[ch] = true;
    }

    // Characterize ADC for voltage calibration
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc_chars);

    adc_initialized = true;
}

// Lazily configure a channel on first read (for gpio analog command).
// WARNING: this switches the pin to analog mode — will break SPI/GPIO on that pin.
static void adc_ensure_channel(int ch)
{
    if (ch < 0 || ch > 9) return;
    if (!adc_chan_configured[ch]) {
        adc1_config_channel_atten((adc1_channel_t)ch, ADC_ATTEN_DB_11);
        adc_chan_configured[ch] = true;
    }
}

int adc_read_mv(int gpio)
{
    if (!adc_initialized) return 0;
    if (gpio < 1 || gpio > 10) return 0;

    int ch = gpio - 1;
    adc_ensure_channel(ch);
    int raw = adc1_get_raw((adc1_channel_t)ch);
    if (raw < 0) return 0;

    return (int)esp_adc_cal_raw_to_voltage(raw, &adc_chars);
}

int adc_read_raw(int gpio)
{
    if (!adc_initialized) return 0;
    if (gpio < 1 || gpio > 10) return 0;

    int ch = gpio - 1;
    adc_ensure_channel(ch);
    int raw = adc1_get_raw((adc1_channel_t)ch);
    return (raw < 0) ? 0 : raw;
}
