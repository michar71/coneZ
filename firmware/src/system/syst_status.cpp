#include "syst_status.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>

#include "driver/temperature_sensor.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "config.h"
#include "conez_wifi.h"
#include "conez_mqtt.h"
#include "gps.h"
#include "lora.h"
#include "main.h"
#include "sensors.h"
#include "loadavg.h"

static node_status s_latest = {};
static bool s_have_latest = false;
static uint32_t s_last_sample_ms = 0;
static std::atomic<bool> s_lora_activity_since_heartbeat{false};
static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_temp_sensor_init_attempted = false;
static uint8_t s_ver_major = 0;
static uint8_t s_ver_minor = 0;

static uint32_t status_interval_ms(void)
{
    uint32_t seconds = (config.mqtt_status_interval > 0)
        ? (uint32_t)config.mqtt_status_interval
        : 30u;
    return seconds * 1000u;
}

static uint8_t clamp_u8_from_tenths(float value)
{
    int iv = (int)lroundf(value * 10.0f);
    if (iv < 0) {
        return 0;
    }
    if (iv > 255) {
        return 255;
    }
    return (uint8_t)iv;
}

static int16_t clamp_i16(float value)
{
    long iv = lroundf(value);
    if (iv < INT16_MIN) {
        return INT16_MIN;
    }
    if (iv > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)iv;
}

static int16_t temp_to_centi(float temp_c)
{
    if (temp_c <= -400.0f) {
        return INT16_MIN;
    }
    long iv = lroundf(temp_c * 100.0f);
    if (iv < INT16_MIN) {
        return INT16_MIN;
    }
    if (iv > INT16_MAX) {
        return INT16_MAX;
    }
    return (int16_t)iv;
}

static uint8_t cpu_load_percent(void)
{
    if (!loadavg_valid()) {
        return 0;
    }

    float cpu_count = (float)portNUM_PROCESSORS;
    if (cpu_count <= 0.0f) {
        return 0;
    }

    int iv = (int)lroundf((loadavg_1() / cpu_count) * 100.0f);
    if (iv < 0) {
        return 0;
    }
    if (iv > 100) {
        return 100;
    }
    return (uint8_t)iv;
}

static uint8_t parse_version_component(const char *text)
{
    if (!text || !*text) {
        return 0;
    }

    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text) {
        return 0;
    }
    if (value > 255ul) {
        return 255;
    }
    return (uint8_t)value;
}

static void cache_firmware_version(void)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    if (!desc) {
        s_ver_major = 0;
        s_ver_minor = 0;
        return;
    }

    char version_copy[sizeof(desc->version)];
    strlcpy(version_copy, desc->version, sizeof(version_copy));

    char *major = version_copy;
    char *minor = strchr(version_copy, '.');
    if (minor) {
        *minor++ = '\0';
    }

    s_ver_major = parse_version_component(major);
    s_ver_minor = parse_version_component(minor);
}

static uint32_t build_status_bits(void)
{
    uint32_t status = (NODE_STATUS_VERSION & 0x0Fu) << 28;

    if (get_gpsstatus()) {
        status |= NODE_STATUS_BIT_GPS_LOCK;
    }
    if (wifi_is_connected()) {
        status |= NODE_STATUS_BIT_WIFI_CONNECTED;
    }
    if (s_lora_activity_since_heartbeat.load(std::memory_order_relaxed)) {
        status |= NODE_STATUS_BIT_LORA_CONNECTED;
    }
    if (syst_status_get_load_power_on()) {
        status |= NODE_STATUS_BIT_LOAD_POWER_ON;
    }
    if (syst_status_get_low_voltage_detected()) {
        status |= NODE_STATUS_BIT_LOW_VOLTAGE_DETECTED;
    }
    if (syst_status_get_low_voltage_disconnect()) {
        status |= NODE_STATUS_BIT_LOW_VOLTAGE_DISCONNECT;
    }
    if (syst_status_get_solar_charging()) {
        status |= NODE_STATUS_BIT_SOLAR_CHARGING;
    }
    if (mqtt_connected()) {
        status |= NODE_STATUS_BIT_MQTT_CONNECTED;
    }

    return status;
}

static void collect_status(node_status *out)
{
    memset(out, 0, sizeof(*out));

    out->status = build_status_bits();
    out->uptime = uptime_ms() / 1000u;
    out->heap = esp_get_free_heap_size();
    out->w_rssi = wifi_is_connected() ? wifi_get_rssi() : 0;
    out->sat_cat = (uint8_t)((get_satellites() < 0) ? 0 : ((get_satellites() > 255) ? 255 : get_satellites()));
    out->v_bat = clamp_u8_from_tenths(bat_voltage());
    out->v_solar = clamp_u8_from_tenths(solar_voltage());
    out->ver_major = s_ver_major;
    out->ver_minor = s_ver_minor;
    out->cpu_load = cpu_load_percent();
    out->b_temp = temp_to_centi(getTemp());
    out->c_temp = syst_status_get_cpu_temp_centi();
    out->tilt_x = imuAvailable() ? getRoll() : 0.0f;
    out->tilt_y = imuAvailable() ? getPitch() : 0.0f;
    wifi_get_ip_bytes(out->ip_addr);

    if (lora_have_rx()) {
        out->l_rssi = (int8_t)lroundf(lora_get_rssi());
        out->l_snr = (int8_t)lroundf(lora_get_snr());
    } else {
        out->l_rssi = 0;
        out->l_snr = 0;
    }

    if (get_gpsstatus()) {
        out->lat = get_lat();
        out->longitude = get_lon();
        out->alt = clamp_i16(get_alt());
    } else {
        out->lat = 0.0f;
        out->longitude = 0.0f;
        out->alt = 0;
    }
}

void syst_status_setup(void)
{
    cache_firmware_version();
    collect_status(&s_latest);
    s_have_latest = true;
    s_last_sample_ms = uptime_ms();
}

void syst_status_loop(void)
{
    uint32_t now = uptime_ms();
    if (!s_have_latest || (uint32_t)(now - s_last_sample_ms) >= status_interval_ms()) {
        collect_status(&s_latest);
        s_have_latest = true;
        s_last_sample_ms = now;
    }
}

bool syst_status_get_latest(node_status *out)
{
    if (!out || !s_have_latest) {
        return false;
    }
    *out = s_latest;
    return true;
}

bool syst_status_get_publishable(node_status *out)
{
    if (!out) {
        return false;
    }
    syst_status_loop();
    if (!s_have_latest) {
        return false;
    }
    *out = s_latest;
    out->status = build_status_bits();
    return true;
}

void syst_status_note_lora_packet(void)
{
    s_lora_activity_since_heartbeat.store(true, std::memory_order_relaxed);
}

void syst_status_on_heartbeat_sent(void)
{
    s_lora_activity_since_heartbeat.store(false, std::memory_order_relaxed);
}

bool syst_status_get_load_power_on(void)
{
    return false;
}

bool syst_status_get_low_voltage_detected(void)
{
    return false;
}

bool syst_status_get_low_voltage_disconnect(void)
{
    return false;
}

bool syst_status_get_solar_charging(void)
{
    return false;
}

int16_t syst_status_get_cpu_temp_centi(void)
{
    if (!s_temp_sensor_init_attempted) {
        s_temp_sensor_init_attempted = true;

        temperature_sensor_config_t cfg = {};
        cfg.range_min = -10;
        cfg.range_max = 80;
        cfg.clk_src = TEMPERATURE_SENSOR_CLK_SRC_DEFAULT;
        cfg.flags.allow_pd = 0;
        if (temperature_sensor_install(&cfg, &s_temp_sensor) == ESP_OK) {
            if (temperature_sensor_enable(s_temp_sensor) != ESP_OK) {
                temperature_sensor_uninstall(s_temp_sensor);
                s_temp_sensor = NULL;
            }
        }
    }

    if (!s_temp_sensor) {
        return INT16_MIN;
    }

    float temp_c = 0.0f;
    if (temperature_sensor_get_celsius(s_temp_sensor, &temp_c) != ESP_OK) {
        return INT16_MIN;
    }

    return temp_to_centi(temp_c);
}
