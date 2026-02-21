#include "adc.h"
#include "board.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali = NULL;
static bool adc_initialized = false;
static bool adc_chan_configured[10] = {};

void adc_setup(void)
{
    // Create oneshot ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {};
    unit_cfg.unit_id = ADC_UNIT_1;
    adc_oneshot_new_unit(&unit_cfg, &adc_handle);

    // Configure channels 0-2 (GPIO 1-3) — safe for ADC, no SPI overlap.
    // GPIO 4+ overlaps with PSRAM SPI (4-7) and LoRa SPI (8-10), so
    // configuring those as ADC would switch the pin to analog and break SPI.
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_12;

    for (int ch = 0; ch < 3; ch++) {
        adc_oneshot_config_channel(adc_handle, (adc_channel_t)ch, &chan_cfg);
        adc_chan_configured[ch] = true;
    }

    // Characterize ADC for voltage calibration (curve fitting on ESP32-S3)
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_12;
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali);

    adc_initialized = true;
}

// Lazily configure a channel on first read (for gpio analog command).
// WARNING: this switches the pin to analog mode — will break SPI/GPIO on that pin.
static void adc_ensure_channel(int ch)
{
    if (ch < 0 || ch > 9) return;
    if (!adc_chan_configured[ch]) {
        adc_oneshot_chan_cfg_t chan_cfg = {};
        chan_cfg.atten = ADC_ATTEN_DB_12;
        chan_cfg.bitwidth = ADC_BITWIDTH_12;
        adc_oneshot_config_channel(adc_handle, (adc_channel_t)ch, &chan_cfg);
        adc_chan_configured[ch] = true;
    }
}

int adc_read_mv(int gpio)
{
    if (!adc_initialized) return 0;
    if (gpio < 1 || gpio > 10) return 0;

    int ch = gpio - 1;
    adc_ensure_channel(ch);

    int raw = 0;
    if (adc_oneshot_read(adc_handle, (adc_channel_t)ch, &raw) != ESP_OK)
        return 0;

    int mv = 0;
    adc_cali_raw_to_voltage(adc_cali, raw, &mv);
    return mv;
}

int adc_read_raw(int gpio)
{
    if (!adc_initialized) return 0;
    if (gpio < 1 || gpio > 10) return 0;

    int ch = gpio - 1;
    adc_ensure_channel(ch);

    int raw = 0;
    if (adc_oneshot_read(adc_handle, (adc_channel_t)ch, &raw) != ESP_OK)
        return 0;

    return raw;
}
