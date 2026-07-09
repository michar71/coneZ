#ifndef SYST_STATUS_H
#define SYST_STATUS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Version encoded into the upper 4 bits of node_status.status.
#define NODE_STATUS_VERSION 0u

// Status bitfield layout.
#define NODE_STATUS_BIT_GPS_LOCK              (1u << 0)
#define NODE_STATUS_BIT_WIFI_CONNECTED        (1u << 1)
#define NODE_STATUS_BIT_LORA_CONNECTED        (1u << 2)
#define NODE_STATUS_BIT_LOAD_POWER_ON         (1u << 3)
#define NODE_STATUS_BIT_LOW_VOLTAGE_DETECTED  (1u << 4)
#define NODE_STATUS_BIT_LOW_VOLTAGE_DISCONNECT (1u << 5)
#define NODE_STATUS_BIT_SOLAR_CHARGING        (1u << 6)
#define NODE_STATUS_BIT_MQTT_CONNECTED        (1u << 7)

struct node_status {
    uint32_t status;
    uint32_t uptime;
    uint32_t heap;
    float lat;
    float longitude;
    float tilt_x;
    float tilt_y;
    int16_t b_temp;
    int16_t c_temp;
    int16_t alt;
    int8_t w_rssi;
    int8_t l_rssi;
    int8_t l_snr;
    uint8_t sat_cat;
    uint8_t v_bat;
    uint8_t v_solar;
    uint8_t cpu_load;
    uint8_t ip_addr[4];
};

static_assert(sizeof(node_status) == 48, "node_status layout changed");

void syst_status_setup(void);
void syst_status_loop(void);
bool syst_status_get_latest(node_status *out);
bool syst_status_get_publishable(node_status *out);

void syst_status_note_lora_packet(void);
void syst_status_on_heartbeat_sent(void);

// Stubbed power/CPU status hooks for future implementation.
bool syst_status_get_load_power_on(void);
bool syst_status_get_low_voltage_detected(void);
bool syst_status_get_low_voltage_disconnect(void);
bool syst_status_get_solar_charging(void);
int16_t syst_status_get_cpu_temp_centi(void);

#endif
