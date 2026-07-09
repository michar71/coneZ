/*
 * conez_mqtt.cpp — MQTT client for ConeZ using ESP-IDF esp_mqtt
 *
 * Wraps esp_mqtt_client with the same public API as before.
 * Auto-reconnects, publishes periodic heartbeats, subscribes
 * to per-cone command topics.
 *
 * The esp_mqtt task runs on core 1 (CONFIG_MQTT_USE_CORE_1).
 *
 * mqtt_publish() is thread-safe and NON-BLOCKING: it copies the message onto a
 * queue and returns. A dedicated publisher task drains that queue and is the only
 * caller of the blocking esp_mqtt_client_publish(). Never call that API directly
 * from a producer -- see the MQTT_PUBLISH_QUEUE_DEPTH comment below for what
 * happens when you do.
 */

#include "esp_system.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>
#include "main.h"
#include "conez_mqtt.h"
#include "conez_wifi.h"
#include "config.h"
#include "printManager.h"
#include "syst_status.h"
#include "node_status.h"

// ---------- Tunables ----------
#define MQTT_KEEPALIVE_SEC   60

// Publish queue. esp_mqtt_client_publish() is the BLOCKING esp-mqtt API: it waits on
// the client's internal api_lock, which the MQTT task holds across reconnect attempts
// and socket writes. Calling it from a producer stalls that producer for as long as
// the broker is unhappy -- measured at 91 s for one call, and unbounded in practice.
//
// mqtt_loop() runs on loopTask, which also blinks the heartbeat LED and services
// http_loop(); the debug sink in printManager calls mqtt_publish() from whichever
// task happened to log. A blocking publish therefore froze the LED, HTTP and the
// board's whole foreground while the LoRa task carried on printing beacons.
//
// So: producers enqueue and return, and only this dedicated task ever blocks inside
// esp-mqtt. A full queue drops the message rather than waiting -- telemetry is
// fire-and-forget (QoS 0) and a stalled broker must never stall the cone.
#define MQTT_PUBLISH_QUEUE_DEPTH   12
#define MQTT_PUBLISH_MAX_PAYLOAD   MQTT_NODE_STATUS_JSON_MAX   // largest thing we publish
#define MQTT_PUBLISH_TOPIC_MAX     64

struct mqtt_msg {
    char     topic[MQTT_PUBLISH_TOPIC_MAX];
    uint16_t len;
    char     payload[];   // NUL-terminated, len bytes
};

// ---------- State ----------
static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static volatile uint32_t s_connected_at = 0;
static volatile uint32_t s_tx_count = 0;
static volatile uint32_t s_rx_count = 0;
static volatile uint32_t s_drop_count = 0;   // messages dropped because the queue was full
static uint32_t last_heartbeat_ms = 0;

static QueueHandle_t s_pub_queue = NULL;     // holds struct mqtt_msg *

static char topic_status[64];
static char topic_cmd[64];
static char client_id[32];

static bool s_started = false;       // esp_mqtt_client_start() has been called
static bool s_user_stopped = false;  // user manually disconnected (suppress auto-start)

static void mqtt_publisher_task(void *arg);

static uint32_t mqtt_heartbeat_interval_ms(void)
{
    uint32_t seconds = (config.mqtt_status_interval > 0)
        ? (uint32_t)config.mqtt_status_interval
        : 30u;
    return seconds * 1000u;
}

// ---------- Event handler ----------

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch (event_id) {

    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        s_connected_at = uptime_ms();
        printfnl(SOURCE_MQTT, "Connected to %s\n", config.mqtt_broker);
        // Subscribe to command topic
        esp_mqtt_client_subscribe(s_client, topic_cmd, 0);
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        s_connected_at = 0;
        printfnl(SOURCE_MQTT, "Disconnected\n");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        printfnl(SOURCE_MQTT, "Subscribed to %s\n", topic_cmd);
        break;

    case MQTT_EVENT_DATA: {
        s_rx_count = s_rx_count + 1;
        // Extract topic and payload (not null-terminated in event)
        char topic[128];
        int tlen = ev->topic_len < (int)sizeof(topic) - 1
                   ? ev->topic_len : (int)sizeof(topic) - 1;
        if (ev->topic && tlen > 0) {
            memcpy(topic, ev->topic, tlen);
            topic[tlen] = '\0';
        } else {
            topic[0] = '\0';
        }

        char payload[256];
        int plen = ev->data_len < (int)sizeof(payload) - 1
                   ? ev->data_len : (int)sizeof(payload) - 1;
        if (ev->data && plen > 0) {
            memcpy(payload, ev->data, plen);
            payload[plen] = '\0';
        } else {
            payload[0] = '\0';
        }

        printfnl(SOURCE_MQTT, "RX [%s] %s\n", topic, payload);
        break;
    }

    case MQTT_EVENT_ERROR:
        printfnl(SOURCE_MQTT, "Error (type=%d)\n",
                 ev->error_handle ? ev->error_handle->error_type : -1);
        break;

    default:
        break;
    }
}

// ---------- Internal helpers ----------

static void mqtt_create_and_start(void)
{
    if (s_client) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", config.mqtt_broker, config.mqtt_port);

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = uri;
    cfg.credentials.client_id = client_id;
    cfg.session.keepalive = MQTT_KEEPALIVE_SEC;
    cfg.network.disable_auto_reconnect = false;
    cfg.network.timeout_ms = 3000;       // default 10s — shorter reduces stop() blocking
    cfg.network.reconnect_timeout_ms = 10000;    // retry after 10s on disconnect
    cfg.buffer.size = 512;
    cfg.buffer.out_size = 512;
    cfg.task.stack_size = 4096;
    cfg.task.priority = 5;

    s_client = esp_mqtt_client_init(&cfg);
    if (!s_client) {
        printfnl(SOURCE_MQTT, "Failed to create client\n");
        return;
    }

    esp_mqtt_client_register_event(s_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    s_started = true;

    printfnl(SOURCE_MQTT, "Client started — broker %s:%d\n",
             config.mqtt_broker, config.mqtt_port);
}

static void mqtt_stop_client(void)
{
    if (s_client && s_started) {
        esp_mqtt_client_stop(s_client);
        s_started = false;
    }
    if (s_client) {
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
    s_connected = false;
    s_connected_at = 0;
}

// ---------- Heartbeat ----------

static void send_heartbeat(void)
{
    if (!s_client) return;

    node_status status = {};
    if (!syst_status_get_publishable(&status)) {
        return;
    }

    char payload[MQTT_NODE_STATUS_JSON_MAX];
    if (mqtt_node_status_format_json(&status, payload, sizeof(payload)) < 0) {
        printfnl(SOURCE_MQTT, "Status JSON build failed\n");
        return;
    }

    // Enqueue rather than publish: this runs on loopTask, which also drives the
    // heartbeat LED and http_loop(). A blocking publish here freezes the board's
    // whole foreground whenever the broker is slow or gone.
    if (mqtt_publish(topic_status, payload) == 0)
        syst_status_on_heartbeat_sent();
    last_heartbeat_ms = uptime_ms();
}

// ---------- Public API ----------

void mqtt_setup(void)
{
    // Build per-cone topic strings
    snprintf(client_id,    sizeof(client_id),    "conez-%d", config.cone_id);
    snprintf(topic_status, sizeof(topic_status), "conez/%d/status", config.cone_id);
    snprintf(topic_cmd,    sizeof(topic_cmd),    "conez/%d/cmd/#", config.cone_id);

    s_tx_count = 0;
    s_rx_count = 0;
    s_drop_count = 0;
    s_user_stopped = false;

    // Publish queue + the only task allowed to block inside esp-mqtt. Created once;
    // mqtt_setup() may run again after a config change.
    if (!s_pub_queue) {
        s_pub_queue = xQueueCreate(MQTT_PUBLISH_QUEUE_DEPTH, sizeof(struct mqtt_msg *));
        if (s_pub_queue) {
            // Core 1 alongside esp_mqtt (CONFIG_MQTT_USE_CORE_1); low priority, since
            // it may sit blocked in a publish for a long time and must not starve
            // anything. Stack covers esp-mqtt's publish path plus our frame.
            xTaskCreatePinnedToCore(mqtt_publisher_task, "mqtt_pub", 4096, NULL, 4, NULL, 1);
        } else {
            printfnl(SOURCE_MQTT, "publish queue alloc failed; telemetry disabled\n");
        }
    }

    printfnl(SOURCE_MQTT, "Client ID: %s, broker: %s\n", client_id, config.mqtt_broker);
    last_heartbeat_ms = 0;
}

void mqtt_loop(void)
{
    // Auto-start when WiFi connects (and MQTT is enabled + not user-stopped)
    if (!s_started && config.mqtt_enabled && !s_user_stopped
        && wifi_is_connected() && config.mqtt_broker[0] != '\0') {
        mqtt_create_and_start();
    }

    // Heartbeat (only when connected)
    if (s_connected && (uptime_ms() - last_heartbeat_ms) >= mqtt_heartbeat_interval_ms()) {
        send_heartbeat();
    }
}

bool mqtt_connected(void)
{
    return s_connected;
}

const char *mqtt_state_str(void)
{
    if (!config.mqtt_enabled)  return "Disabled";
    if (s_connected)           return "Connected";
    if (s_started)             return "Connecting";
    return "Disconnected";
}

uint32_t mqtt_uptime_sec(void)
{
    if (!s_connected || s_connected_at == 0) return 0;
    return (uptime_ms() - s_connected_at) / 1000;
}

uint32_t mqtt_tx_count(void) { return s_tx_count; }
uint32_t mqtt_rx_count(void) { return s_rx_count; }

uint32_t mqtt_dropped_count(void)
{
    return s_drop_count;
}

void mqtt_force_connect(void)
{
    s_user_stopped = false;
    if (!s_started && wifi_is_connected()) {
        mqtt_create_and_start();
    } else if (s_client) {
        esp_mqtt_client_reconnect(s_client);
    }
}

void mqtt_force_disconnect(void)
{
    s_user_stopped = true;
    mqtt_stop_client();
    printfnl(SOURCE_MQTT, "Disconnected (user)\n");
}

// The one place esp_mqtt_client_publish() may be called from. Blocking here is
// harmless: nothing else waits on this task.
static void mqtt_publisher_task(void *arg)
{
    (void)arg;
    for (;;) {
        struct mqtt_msg *m = NULL;
        if (xQueueReceive(s_pub_queue, &m, portMAX_DELAY) != pdTRUE || !m)
            continue;

        // Re-check: the broker may have dropped while this sat in the queue.
        if (s_connected && s_client) {
            int msg_id = esp_mqtt_client_publish(s_client, m->topic, m->payload, m->len, 0, 0);
            if (msg_id >= 0)
                s_tx_count = s_tx_count + 1;
        }
        free(m);
    }
}

int mqtt_publish(const char *topic, const char *payload)
{
    if (!s_connected || !s_client || !s_pub_queue) return -1;

    size_t plen = strlen(payload);
    if (plen > MQTT_PUBLISH_MAX_PAYLOAD) return -1;

    struct mqtt_msg *m = (struct mqtt_msg *)malloc(sizeof(*m) + plen + 1);
    if (!m) { s_drop_count = s_drop_count + 1; return -1; }

    strlcpy(m->topic, topic, sizeof(m->topic));
    memcpy(m->payload, payload, plen + 1);
    m->len = (uint16_t)plen;

    // Zero wait. A producer must never block on the broker -- that is the entire
    // point of this queue. Dropping QoS-0 telemetry is the correct trade.
    if (xQueueSend(s_pub_queue, &m, 0) != pdTRUE) {
        free(m);
        s_drop_count = s_drop_count + 1;
        return -1;
    }
    return 0;
}
