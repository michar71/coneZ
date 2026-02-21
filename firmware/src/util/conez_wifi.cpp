/*
 * conez_wifi.cpp — Centralized WiFi management using ESP-IDF APIs
 *
 * Replaces all Arduino WiFi.* usage. Event-driven state tracking,
 * info queries via esp_wifi / esp_netif APIs.
 */

#include "conez_wifi.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "lwip/netif.h"
#include "main.h"
#include <string.h>

// ---------- State ----------

static volatile wifi_state_e s_state = WIFI_ST_OFF;
static volatile uint32_t     s_connected_since = 0;  // uptime_ms at IP_EVENT_STA_GOT_IP
static esp_netif_t          *s_sta_netif = NULL;
static bool                  s_initialized = false;
static char                  s_hostname[64] = "";

// ---------- Byte counting ----------
// ESP-IDF's wlanif.c doesn't increment LWIP's MIB2 byte counters,
// so we wrap the netif's linkoutput (TX) and input (RX) to count bytes.

static volatile uint32_t s_tx_bytes = 0;
static volatile uint32_t s_rx_bytes = 0;
static netif_linkoutput_fn s_orig_linkoutput = NULL;
static netif_input_fn      s_orig_input = NULL;
static bool                s_wrappers_installed = false;

static err_t IRAM_ATTR counted_linkoutput(struct netif *netif, struct pbuf *p)
{
    s_tx_bytes += p->tot_len;
    return s_orig_linkoutput(netif, p);
}

static err_t IRAM_ATTR counted_input(struct pbuf *p, struct netif *inp)
{
    s_rx_bytes += p->tot_len;
    return s_orig_input(p, inp);
}

static void install_byte_counting(void)
{
    if (s_wrappers_installed || !s_sta_netif) return;
    struct netif *nif = (struct netif *)esp_netif_get_netif_impl(s_sta_netif);
    if (!nif) return;

    s_orig_linkoutput = nif->linkoutput;
    s_orig_input = nif->input;
    nif->linkoutput = counted_linkoutput;
    nif->input = counted_input;
    s_wrappers_installed = true;
}

// ---------- Event handlers ----------

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_state != WIFI_ST_OFF)
                s_state = WIFI_ST_DISCONNECTED;
            s_connected_since = 0;
            break;
        case WIFI_EVENT_STA_START:
            s_state = WIFI_ST_CONNECTING;
            break;
        case WIFI_EVENT_STA_STOP:
            s_state = WIFI_ST_OFF;
            s_connected_since = 0;
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            s_state = WIFI_ST_CONNECTED;
            s_connected_since = uptime_ms();
            install_byte_counting();
        }
    }
}

// ---------- Public API ----------

void wifi_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    esp_netif_init();
    // Event loop may already exist (Arduino creates it) — ignore error
    esp_event_loop_create_default();

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               wifi_event_handler, NULL);
}

void wifi_start(const char *ssid, const char *password, const char *hostname)
{
    if (!s_initialized) wifi_init();

    // Set hostname before connect (must happen before DHCP)
    if (hostname && hostname[0]) {
        strlcpy(s_hostname, hostname, sizeof(s_hostname));
        esp_netif_set_hostname(s_sta_netif, s_hostname);
    }

    esp_wifi_set_mode(WIFI_MODE_STA);

    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);

    s_state = WIFI_ST_CONNECTING;
    esp_wifi_start();
    esp_wifi_connect();
}

void wifi_stop(void)
{
    esp_wifi_disconnect();
    esp_wifi_stop();
    s_state = WIFI_ST_OFF;
    s_connected_since = 0;
}

void wifi_reconnect(const char *ssid, const char *password)
{
    if (!s_initialized) wifi_init();

    esp_wifi_disconnect();

    wifi_config_t wcfg = {};
    strlcpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
    strlcpy((char *)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wcfg);

    s_state = WIFI_ST_CONNECTING;
    esp_wifi_connect();
}

wifi_state_e wifi_get_state(void)
{
    return s_state;
}

bool wifi_is_connected(void)
{
    return s_state == WIFI_ST_CONNECTED;
}

const char *wifi_state_str(void)
{
    switch (s_state) {
    case WIFI_ST_OFF:            return "Off";
    case WIFI_ST_DISCONNECTED:   return "Disconnected";
    case WIFI_ST_CONNECTING:     return "Connecting";
    case WIFI_ST_CONNECTED:      return "Connected";
    case WIFI_ST_NO_SSID:        return "SSID not found";
    case WIFI_ST_CONNECT_FAILED: return "Connect failed";
    default:                     return "Unknown";
    }
}

void wifi_get_ip_str(char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info;
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        strlcpy(buf, "0.0.0.0", len);
    }
}

void wifi_get_gateway_str(char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info;
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.gw));
    } else {
        strlcpy(buf, "0.0.0.0", len);
    }
}

void wifi_get_subnet_str(char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info;
    if (s_sta_netif && esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.netmask));
    } else {
        strlcpy(buf, "0.0.0.0", len);
    }
}

void wifi_get_dns_str(char *buf, size_t len)
{
    esp_netif_dns_info_t dns_info;
    if (s_sta_netif &&
        esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
    } else {
        strlcpy(buf, "0.0.0.0", len);
    }
}

void wifi_get_mac(uint8_t mac[6])
{
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

int8_t wifi_get_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        return ap.rssi;
    return 0;
}

uint8_t wifi_get_channel(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
        return ap.primary;
    return 0;
}

void wifi_get_ssid(char *buf, size_t len)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        strlcpy(buf, (const char *)ap.ssid, len);
    } else {
        buf[0] = '\0';
    }
}

void wifi_get_bssid_str(char *buf, size_t len)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(buf, len, "%02X:%02X:%02X:%02X:%02X:%02X",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2],
                 ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    } else {
        strlcpy(buf, "00:00:00:00:00:00", len);
    }
}

const char *wifi_get_hostname(void)
{
    const char *hn = NULL;
    if (s_sta_netif)
        esp_netif_get_hostname(s_sta_netif, &hn);
    return hn ? hn : "";
}

int8_t wifi_get_tx_power_dbm(void)
{
    int8_t power;
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK)
        return power;  // in 0.25 dBm units (same as Arduino getTxPower())
    return 0;
}

uint32_t wifi_get_connected_since(void)
{
    return s_connected_since;
}

bool wifi_get_byte_counts(uint32_t *tx_bytes, uint32_t *rx_bytes)
{
    if (!s_wrappers_installed) return false;
    *tx_bytes = s_tx_bytes;
    *rx_bytes = s_rx_bytes;
    return true;
}
