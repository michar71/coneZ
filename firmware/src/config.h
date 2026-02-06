#ifndef _conez_config_h
#define _conez_config_h

#include <Arduino.h>

// ---------- String-length limits ----------
#define CONFIG_MAX_SSID         33
#define CONFIG_MAX_PASSWORD     65
#define CONFIG_MAX_DEVICE_NAME  33
#define CONFIG_MAX_PATH         33
#define CONFIG_MAX_LORA_SSID    17
#define CONFIG_MAX_NTP_SERVER   65
#define CONFIG_MAX_CALLSIGN     11

// ---------- Compiled defaults ----------
// WiFi
#define DEFAULT_WIFI_SSID       "RN-ConeZ"
#define DEFAULT_WIFI_PASSWORD   "conezconez"

// GPS origin (Nevada desert festival)
#define DEFAULT_ORIGIN_LAT      40.762173f
#define DEFAULT_ORIGIN_LON      -119.193672f

// LoRa radio
#define DEFAULT_LORA_FREQUENCY  431.250f
#define DEFAULT_LORA_BANDWIDTH  500.0f
#define DEFAULT_LORA_SF         9
#define DEFAULT_LORA_CR         6
#define DEFAULT_LORA_PREAMBLE   8
#define DEFAULT_LORA_TX_POWER   10
#define DEFAULT_LORA_SYNC_WORD  0x12            // 0x12 = default, 0x1424 = private
#define DEFAULT_LORA_SSID       "ConeZ"
#define DEFAULT_LORA_CALLSIGN   ""

// System
#define DEFAULT_DEVICE_NAME     ""
#define DEFAULT_STARTUP_SCRIPT  "/startup.bas"
#define DEFAULT_TIMEZONE        -8              // Standard (winter) offset; DST adds +1 automatically
#define DEFAULT_AUTO_DST        true
#define DEFAULT_CONE_ID         0
#define DEFAULT_CONE_GROUP      0
#define DEFAULT_NTP_SERVER      ""              // empty = use pool.ntp.org + time.nist.gov

// LED counts per channel
#define DEFAULT_LED_COUNT       50

// Debug (true = on at boot)
#define DEFAULT_DBG_SYSTEM      true
#define DEFAULT_DBG_BASIC       true
#define DEFAULT_DBG_COMMANDS    true
#define DEFAULT_DBG_GPS         false
#define DEFAULT_DBG_GPS_RAW     false
#define DEFAULT_DBG_LORA        false
#define DEFAULT_DBG_LORA_RAW    false
#define DEFAULT_DBG_FSYNC       false
#define DEFAULT_DBG_WIFI        false
#define DEFAULT_DBG_SENSORS     false
#define DEFAULT_DBG_OTHER       false

// ---------- Config struct ----------
typedef struct {
    // [wifi]
    char    wifi_ssid[CONFIG_MAX_SSID];
    char    wifi_password[CONFIG_MAX_PASSWORD];

    // [gps]
    float   origin_lat;
    float   origin_lon;

    // [lora]
    float   lora_frequency;
    float   lora_bandwidth;
    int     lora_sf;
    int     lora_cr;
    int     lora_preamble;
    int     lora_tx_power;
    int     lora_sync_word;
    char    lora_ssid[CONFIG_MAX_LORA_SSID];
    char    lora_callsign[CONFIG_MAX_CALLSIGN];

    // [system]
    char    device_name[CONFIG_MAX_DEVICE_NAME];
    char    startup_script[CONFIG_MAX_PATH];
    int     timezone;
    bool    auto_dst;
    int     cone_id;
    int     cone_group;
    char    ntp_server[CONFIG_MAX_NTP_SERVER];

    // [led]
    int     led_count1;
    int     led_count2;
    int     led_count3;
    int     led_count4;

    // [debug]
    bool    dbg_system;
    bool    dbg_basic;
    bool    dbg_commands;
    bool    dbg_gps;
    bool    dbg_gps_raw;
    bool    dbg_lora;
    bool    dbg_lora_raw;
    bool    dbg_fsync;
    bool    dbg_wifi;
    bool    dbg_sensors;
    bool    dbg_other;
} conez_config_t;

// ---------- Global config instance ----------
extern conez_config_t config;

// ---------- Public API ----------
void config_init(void);
void config_save(void);
void config_reset(void);
void config_apply_debug(void);
int  cmd_config(int argc, char **argv);

// ---------- Web interface ----------
class WebServer;   // forward-declare
String config_get_html(const char *msg);
void   config_set_from_web(WebServer &server);

#endif
