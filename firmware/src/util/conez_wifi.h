#ifndef CONEZ_WIFI_H
#define CONEZ_WIFI_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    WIFI_ST_OFF,
    WIFI_ST_DISCONNECTED,
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,
    WIFI_ST_NO_SSID,
    WIFI_ST_CONNECT_FAILED
} wifi_state_e;

// Call once in setup() — creates netif, event loop, event handlers
void wifi_init(void);

// Connect as STA with the given credentials and hostname
void wifi_start(const char *ssid, const char *password, const char *hostname);

// Disconnect and power down the radio
void wifi_stop(void);

// Reconnect with new credentials (keeps STA mode active)
void wifi_reconnect(const char *ssid, const char *password);

// State queries
wifi_state_e wifi_get_state(void);
bool         wifi_is_connected(void);
const char  *wifi_state_str(void);

// Info queries (valid when connected)
void     wifi_get_ip_str(char *buf, size_t len);
void     wifi_get_gateway_str(char *buf, size_t len);
void     wifi_get_subnet_str(char *buf, size_t len);
void     wifi_get_dns_str(char *buf, size_t len);
void     wifi_get_mac(uint8_t mac[6]);
int8_t   wifi_get_rssi(void);
uint8_t  wifi_get_channel(void);
void     wifi_get_ssid(char *buf, size_t len);
void     wifi_get_bssid_str(char *buf, size_t len);
const char *wifi_get_hostname(void);
int8_t   wifi_get_tx_power_dbm(void);
uint32_t wifi_get_connected_since(void);  // uptime_ms at connect, 0 if not
bool     wifi_get_byte_counts(uint32_t *tx_bytes, uint32_t *rx_bytes);

#endif
