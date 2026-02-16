/*
 * mqtt_client.cpp — Minimal MQTT 3.1.1 client for ConeZ
 *
 * Connects to the sewerpipe broker over WiFi, auto-reconnects with
 * exponential backoff, publishes periodic heartbeats, and subscribes
 * to per-cone command topics.
 *
 * All state is file-static. Runs entirely on loopTask (core 1).
 * ShellTask (also core 1, time-sliced) can call mqtt_publish() and
 * the force_connect/disconnect setters — mqtt_publish() uses its own
 * local buffer to avoid racing on tx_buf.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "mqtt_client.h"
#include "config.h"
#include "printManager.h"
#include "sensors.h"

// ---------- MQTT 3.1.1 packet types ----------
#define MQTT_CONNECT      1
#define MQTT_CONNACK      2
#define MQTT_PUBLISH      3
#define MQTT_SUBSCRIBE    8
#define MQTT_SUBACK       9
#define MQTT_PINGREQ     12
#define MQTT_PINGRESP    13
#define MQTT_DISCONNECT  14

// ---------- Tunables ----------
#define MQTT_KEEPALIVE_SEC   60
#define MQTT_HEARTBEAT_MS    30000
#define MQTT_CONNACK_TIMEOUT 5000
#define MQTT_PINGRESP_TIMEOUT (MQTT_KEEPALIVE_SEC * 1500)  // 1.5x keepalive in ms
#define MQTT_BACKOFF_INIT    1000
#define MQTT_BACKOFF_MAX     30000
#define MQTT_BUF_SIZE        512

// ---------- State ----------
typedef enum {
    ST_DISCONNECTED,
    ST_WAIT_CONNACK,
    ST_CONNECTED
} mqtt_state_e;

static WiFiClient tcp;
static mqtt_state_e state = ST_DISCONNECTED;

static uint32_t last_attempt_ms;
static uint32_t reconnect_delay_ms = MQTT_BACKOFF_INIT;
static uint32_t connected_at_ms;
static uint32_t last_heartbeat_ms;
static uint32_t last_pingreq_ms;
static uint32_t last_pingresp_ms;

static uint32_t s_tx_count;
static uint32_t s_rx_count;
static uint16_t next_msg_id = 1;

static uint8_t rx_buf[MQTT_BUF_SIZE];
static int     rx_len;
static uint8_t tx_buf[MQTT_BUF_SIZE];

static char topic_status[64];
static char topic_cmd[64];
static char client_id[32];

static bool force_connect_flag;
static bool force_disconnect_flag;
static bool user_disconnected;  // true when user manually disconnects (suppresses auto-reconnect)

// ---------- Wire format helpers ----------

static int write_remaining_length(uint8_t *buf, uint32_t value)
{
    int n = 0;
    do {
        uint8_t b = value % 128;
        value /= 128;
        if (value > 0) b |= 0x80;
        buf[n++] = b;
    } while (value > 0);
    return n;
}

// Returns 1 on success, 0 if incomplete, -1 on error
static int read_remaining_length(const uint8_t *buf, int len,
                                 uint32_t *value, int *consumed)
{
    uint32_t multiplier = 1;
    uint32_t val = 0;
    int i = 0;
    do {
        if (i >= len) return 0;
        if (i >= 4) return -1;
        val += (buf[i] & 0x7F) * multiplier;
        multiplier *= 128;
    } while (buf[i++] & 0x80);
    *value = val;
    *consumed = i;
    return 1;
}

static int write_utf8(uint8_t *buf, const char *str)
{
    uint16_t slen = (uint16_t)strlen(str);
    buf[0] = (slen >> 8) & 0xFF;
    buf[1] = slen & 0xFF;
    memcpy(buf + 2, str, slen);
    return 2 + slen;
}

// ---------- Packet builders ----------

static int build_connect(uint8_t *buf, int bufsize)
{
    // Variable header + payload into temp area
    uint8_t body[128];
    int p = 0;

    // Protocol name "MQTT"
    p += write_utf8(body + p, "MQTT");
    // Protocol level 4 (MQTT 3.1.1)
    body[p++] = 0x04;
    // Connect flags: clean session (0x02)
    body[p++] = 0x02;
    // Keep alive
    body[p++] = (MQTT_KEEPALIVE_SEC >> 8) & 0xFF;
    body[p++] = MQTT_KEEPALIVE_SEC & 0xFF;
    // Payload: client ID
    p += write_utf8(body + p, client_id);

    // Fixed header
    uint8_t rem_buf[4];
    int rem_bytes = write_remaining_length(rem_buf, p);
    int total = 1 + rem_bytes + p;
    if (total > bufsize) return -1;

    int off = 0;
    buf[off++] = (MQTT_CONNECT << 4);
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;
    memcpy(buf + off, body, p);

    return off + p;
}

static int build_subscribe(uint8_t *buf, int bufsize, const char *filter, uint8_t qos)
{
    uint8_t body[128];
    int p = 0;

    // Message ID
    uint16_t mid = next_msg_id++;
    if (next_msg_id == 0) next_msg_id = 1;
    body[p++] = (mid >> 8) & 0xFF;
    body[p++] = mid & 0xFF;

    // Topic filter
    p += write_utf8(body + p, filter);
    // Requested QoS
    body[p++] = qos;

    // Fixed header
    uint8_t rem_buf[4];
    int rem_bytes = write_remaining_length(rem_buf, p);
    int total = 1 + rem_bytes + p;
    if (total > bufsize) return -1;

    int off = 0;
    buf[off++] = (MQTT_SUBSCRIBE << 4) | 0x02;  // bit 1 must be set per spec
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;
    memcpy(buf + off, body, p);

    return off + p;
}

// Build a PUBLISH packet into the given buffer. Returns total bytes, or -1 on overflow.
static int build_publish_buf(uint8_t *buf, int bufsize,
                             const char *topic, const char *payload, bool retain)
{
    uint16_t tlen = (uint16_t)strlen(topic);
    uint32_t plen = (uint32_t)strlen(payload);
    uint32_t rem_len = 2 + tlen + plen;  // QoS 0, no msg_id

    // Calculate total size before writing anything
    uint8_t rem_buf[4];
    int rem_bytes = write_remaining_length(rem_buf, rem_len);
    int total = 1 + rem_bytes + rem_len;
    if (total > bufsize) return -1;

    int off = 0;
    uint8_t flags = 0;
    if (retain) flags |= 0x01;
    buf[off++] = (MQTT_PUBLISH << 4) | flags;
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;

    // Topic
    buf[off++] = (tlen >> 8) & 0xFF;
    buf[off++] = tlen & 0xFF;
    memcpy(buf + off, topic, tlen);
    off += tlen;

    // Payload
    if (plen > 0) {
        memcpy(buf + off, payload, plen);
        off += plen;
    }

    return off;
}

static int build_pingreq(uint8_t *buf)
{
    buf[0] = (MQTT_PINGREQ << 4);
    buf[1] = 0x00;
    return 2;
}

static int build_disconnect(uint8_t *buf)
{
    buf[0] = (MQTT_DISCONNECT << 4);
    buf[1] = 0x00;
    return 2;
}

// ---------- Send helper ----------

static bool mqtt_send(const uint8_t *buf, int len)
{
    if (len <= 0) return false;
    int written = tcp.write(buf, len);
    if (written == len) {
        s_tx_count++;
        return true;
    }
    // Partial write — disconnect to avoid corrupt MQTT stream
    printfnl(SOURCE_MQTT, "Partial write (%d/%d), disconnecting\n", written, len);
    tcp.stop();
    state = ST_DISCONNECTED;
    rx_len = 0;
    return false;
}

// ---------- Incoming handlers ----------

static void handle_connack(const uint8_t *payload, uint32_t plen)
{
    if (plen < 2) {
        printfnl(SOURCE_MQTT, "CONNACK too short\n");
        return;
    }

    uint8_t rc = payload[1];
    if (rc != 0) {
        printfnl(SOURCE_MQTT, "CONNACK rejected (rc=%d)\n", rc);
        tcp.stop();
        state = ST_DISCONNECTED;
        return;
    }

    state = ST_CONNECTED;
    connected_at_ms = millis();
    last_heartbeat_ms = 0;
    last_pingreq_ms = millis();
    last_pingresp_ms = millis();
    reconnect_delay_ms = MQTT_BACKOFF_INIT;
    user_disconnected = false;

    printfnl(SOURCE_MQTT, "Connected to %s\n", config.mqtt_broker);

    // Subscribe to command topic
    int len = build_subscribe(tx_buf, MQTT_BUF_SIZE, topic_cmd, 0);
    if (len > 0) mqtt_send(tx_buf, len);
}

static void handle_publish(uint8_t flags, const uint8_t *data, uint32_t dlen)
{
    if (dlen < 2) return;

    uint8_t qos = (flags >> 1) & 3;

    uint16_t tlen = (data[0] << 8) | data[1];
    if (2 + tlen > dlen) return;

    // Extract topic (not null-terminated in wire format)
    char topic[128];
    int copy_len = tlen < (int)sizeof(topic) - 1 ? tlen : (int)sizeof(topic) - 1;
    memcpy(topic, data + 2, copy_len);
    topic[copy_len] = '\0';

    uint32_t pos = 2 + tlen;

    // Skip message ID for QoS 1/2
    if (qos > 0) {
        if (pos + 2 > dlen) return;
        // We don't send PUBACK (we subscribed at QoS 0, so broker
        // should not send QoS 1, but handle it defensively)
        pos += 2;
    }

    // Payload
    const uint8_t *pdata = data + pos;
    uint32_t plen = dlen - pos;

    char payload_str[256];
    int plen_copy = plen < sizeof(payload_str) - 1 ? plen : sizeof(payload_str) - 1;
    memcpy(payload_str, pdata, plen_copy);
    payload_str[plen_copy] = '\0';

    s_rx_count++;
    printfnl(SOURCE_MQTT, "RX [%s] %s\n", topic, payload_str);
}

static void handle_suback(const uint8_t *data, uint32_t dlen)
{
    if (dlen < 3) return;
    uint8_t rc = data[2];  // first (only) return code
    if (rc == 0x80) {
        printfnl(SOURCE_MQTT, "SUBACK: subscription rejected\n");
    } else {
        printfnl(SOURCE_MQTT, "Subscribed to %s (qos=%d)\n", topic_cmd, rc);
    }
}

// ---------- Parser ----------

// Returns bytes consumed (>0), 0 if incomplete, -1 on error
static int parse_and_dispatch(void)
{
    if (rx_len < 2) return 0;

    uint8_t pkt_type = (rx_buf[0] >> 4) & 0x0F;
    uint8_t flags = rx_buf[0] & 0x0F;

    uint32_t rem_len;
    int len_bytes;
    int rc = read_remaining_length(rx_buf + 1, rx_len - 1, &rem_len, &len_bytes);
    if (rc == 0) return 0;   // incomplete
    if (rc < 0) return -1;   // malformed

    uint32_t total = 1 + len_bytes + rem_len;
    if ((uint32_t)rx_len < total) return 0;  // incomplete

    const uint8_t *payload = rx_buf + 1 + len_bytes;

    switch (pkt_type) {
    case MQTT_CONNACK:
        handle_connack(payload, rem_len);
        break;
    case MQTT_PUBLISH:
        handle_publish(flags, payload, rem_len);
        break;
    case MQTT_SUBACK:
        handle_suback(payload, rem_len);
        break;
    case MQTT_PINGRESP:
        last_pingresp_ms = millis();
        break;
    default:
        printfnl(SOURCE_MQTT, "Unknown packet type %d\n", pkt_type);
        break;
    }

    return (int)total;
}

// ---------- Heartbeat ----------

static void send_heartbeat(void)
{
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"uptime\":%lu,\"heap\":%lu,\"temp\":%.1f,\"rssi\":%d}",
             (unsigned long)(millis() / 1000),
             (unsigned long)ESP.getFreeHeap(),
             getTemp(),
             WiFi.RSSI());

    int len = build_publish_buf(tx_buf, MQTT_BUF_SIZE, topic_status, payload, false);
    if (len > 0) mqtt_send(tx_buf, len);
    last_heartbeat_ms = millis();
}

// ---------- Read from TCP ----------

static void mqtt_read(void)
{
    int avail = tcp.available();
    if (avail <= 0) return;

    int space = MQTT_BUF_SIZE - rx_len;
    if (space <= 0) {
        // Buffer full — protocol error
        printfnl(SOURCE_MQTT, "RX buffer overflow, disconnecting\n");
        tcp.stop();
        state = ST_DISCONNECTED;
        return;
    }

    int to_read = avail < space ? avail : space;
    int got = tcp.read(rx_buf + rx_len, to_read);
    if (got > 0) rx_len += got;

    // Parse as many complete packets as we have
    for (;;) {
        int consumed = parse_and_dispatch();
        if (consumed <= 0) {
            if (consumed < 0) {
                printfnl(SOURCE_MQTT, "Protocol error, disconnecting\n");
                tcp.stop();
                state = ST_DISCONNECTED;
            }
            break;
        }
        // Shift remaining data
        rx_len -= consumed;
        if (rx_len > 0)
            memmove(rx_buf, rx_buf + consumed, rx_len);
    }
}

// ---------- Disconnect helper ----------

static void do_disconnect(void)
{
    if (tcp.connected()) {
        int len = build_disconnect(tx_buf);
        mqtt_send(tx_buf, len);
        tcp.stop();
    }
    state = ST_DISCONNECTED;
    rx_len = 0;
}

// ---------- Public API ----------

void mqtt_setup(void)
{
    // Build per-cone topic strings
    snprintf(client_id,    sizeof(client_id),    "conez-%d", config.cone_id);
    snprintf(topic_status, sizeof(topic_status), "conez/%d/status", config.cone_id);
    snprintf(topic_cmd,    sizeof(topic_cmd),    "conez/%d/cmd/#", config.cone_id);

    state = ST_DISCONNECTED;
    rx_len = 0;
    s_tx_count = 0;
    s_rx_count = 0;
    reconnect_delay_ms = MQTT_BACKOFF_INIT;
    user_disconnected = false;

    printfnl(SOURCE_MQTT, "Client ID: %s, broker: %s\n", client_id, config.mqtt_broker);
}

void mqtt_loop(void)
{
    uint32_t now = millis();

    // Handle force flags from ShellTask
    if (force_disconnect_flag) {
        force_disconnect_flag = false;
        user_disconnected = true;
        if (state != ST_DISCONNECTED) {
            do_disconnect();
            printfnl(SOURCE_MQTT, "Disconnected (forced)\n");
        }
        return;
    }

    if (force_connect_flag) {
        force_connect_flag = false;
        user_disconnected = false;
        if (state == ST_DISCONNECTED) {
            last_attempt_ms = 0;  // skip backoff
            reconnect_delay_ms = 0;
        }
    }

    switch (state) {

    case ST_DISCONNECTED: {
        if (!config.mqtt_enabled) return;
        if (user_disconnected) return;
        if (WiFi.status() != WL_CONNECTED) return;
        if (config.mqtt_broker[0] == '\0') return;

        // Backoff check
        if (reconnect_delay_ms > 0 && (now - last_attempt_ms) < reconnect_delay_ms)
            return;

        last_attempt_ms = now;
        printfnl(SOURCE_MQTT, "Connecting to %s:%d...\n", config.mqtt_broker, config.mqtt_port);

        if (!tcp.connect(config.mqtt_broker, config.mqtt_port)) {
            // Double backoff, cap at max
            if (reconnect_delay_ms < MQTT_BACKOFF_INIT)
                reconnect_delay_ms = MQTT_BACKOFF_INIT;
            else
                reconnect_delay_ms = reconnect_delay_ms * 2;
            if (reconnect_delay_ms > MQTT_BACKOFF_MAX)
                reconnect_delay_ms = MQTT_BACKOFF_MAX;
            printfnl(SOURCE_MQTT, "TCP connect failed (retry in %lus)\n",
                     (unsigned long)(reconnect_delay_ms / 1000));
            return;
        }

        // TCP connected — send CONNECT packet
        int len = build_connect(tx_buf, MQTT_BUF_SIZE);
        if (len <= 0 || !mqtt_send(tx_buf, len)) {
            tcp.stop();
            return;
        }
        state = ST_WAIT_CONNACK;
        last_attempt_ms = millis();
        break;
    }

    case ST_WAIT_CONNACK: {
        if ((now - last_attempt_ms) > MQTT_CONNACK_TIMEOUT) {
            printfnl(SOURCE_MQTT, "CONNACK timeout, disconnecting\n");
            tcp.stop();
            state = ST_DISCONNECTED;
            last_attempt_ms = millis();
            // Apply backoff
            if (reconnect_delay_ms < MQTT_BACKOFF_INIT)
                reconnect_delay_ms = MQTT_BACKOFF_INIT;
            else
                reconnect_delay_ms = reconnect_delay_ms * 2;
            if (reconnect_delay_ms > MQTT_BACKOFF_MAX)
                reconnect_delay_ms = MQTT_BACKOFF_MAX;
            return;
        }
        mqtt_read();
        break;
    }

    case ST_CONNECTED: {
        if (!tcp.connected()) {
            printfnl(SOURCE_MQTT, "Connection lost\n");
            state = ST_DISCONNECTED;
            rx_len = 0;
            reconnect_delay_ms = MQTT_BACKOFF_INIT;
            return;
        }

        // PINGRESP timeout — broker stopped responding
        if ((now - last_pingresp_ms) > MQTT_PINGRESP_TIMEOUT) {
            printfnl(SOURCE_MQTT, "PINGRESP timeout, disconnecting\n");
            tcp.stop();
            state = ST_DISCONNECTED;
            rx_len = 0;
            reconnect_delay_ms = MQTT_BACKOFF_INIT;
            return;
        }

        // PINGREQ at keepalive/2
        if ((now - last_pingreq_ms) >= (MQTT_KEEPALIVE_SEC * 500)) {
            int len = build_pingreq(tx_buf);
            mqtt_send(tx_buf, len);
            last_pingreq_ms = now;
        }

        // Heartbeat
        if ((now - last_heartbeat_ms) >= MQTT_HEARTBEAT_MS) {
            send_heartbeat();
        }

        // Read incoming
        mqtt_read();
        break;
    }

    default:
        state = ST_DISCONNECTED;
        break;
    }
}

bool mqtt_connected(void)
{
    return state == ST_CONNECTED;
}

const char *mqtt_state_str(void)
{
    switch (state) {
    case ST_DISCONNECTED:  return "Disconnected";
    case ST_WAIT_CONNACK:  return "Waiting for CONNACK";
    case ST_CONNECTED:     return "Connected";
    default:               return "Unknown";
    }
}

uint32_t mqtt_uptime_sec(void)
{
    if (state != ST_CONNECTED) return 0;
    return (millis() - connected_at_ms) / 1000;
}

uint32_t mqtt_tx_count(void) { return s_tx_count; }
uint32_t mqtt_rx_count(void) { return s_rx_count; }

void mqtt_force_connect(void)
{
    force_connect_flag = true;
}

void mqtt_force_disconnect(void)
{
    force_disconnect_flag = true;
}

int mqtt_publish(const char *topic, const char *payload)
{
    if (state != ST_CONNECTED) return -1;
    // Use a local buffer to avoid racing with loopTask on tx_buf
    uint8_t pub_buf[MQTT_BUF_SIZE];
    int len = build_publish_buf(pub_buf, MQTT_BUF_SIZE, topic, payload, false);
    if (len <= 0) return -1;
    // tcp.write is safe from ShellTask (same core as loopTask, time-sliced)
    int written = tcp.write(pub_buf, len);
    if (written == len) {
        s_tx_count++;
        return 0;
    }
    return -1;
}
