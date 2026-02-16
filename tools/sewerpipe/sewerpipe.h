/*
 * sewerpipe.h — Bare-bones MQTT 3.1.1 broker
 */
#ifndef SEWERPIPE_H
#define SEWERPIPE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ---------- Constants ---------- */

#define MAX_CLIENTS         128
#define MAX_SUBS_PER_CLIENT  32
#define MAX_RETAINED        256
#define MAX_INFLIGHT         16
#define RX_BUF_SIZE       65536
#define MAX_TOPIC_LEN       256
#define MAX_PAYLOAD_SIZE  65536
#define RETRY_INTERVAL_SEC    5
#define DEFAULT_PORT       1883

/* MQTT packet types */
#define MQTT_CONNECT      1
#define MQTT_CONNACK      2
#define MQTT_PUBLISH      3
#define MQTT_PUBACK       4
#define MQTT_SUBSCRIBE    8
#define MQTT_SUBACK       9
#define MQTT_UNSUBSCRIBE 10
#define MQTT_UNSUBACK    11
#define MQTT_PINGREQ     12
#define MQTT_PINGRESP    13
#define MQTT_DISCONNECT  14

/* CONNACK return codes */
#define CONNACK_ACCEPTED              0
#define CONNACK_UNACCEPTABLE_PROTOCOL 1
#define CONNACK_IDENTIFIER_REJECTED   2
#define CONNACK_SERVER_UNAVAILABLE    3

/* ---------- Data Structures ---------- */

typedef struct {
    char    topic[MAX_TOPIC_LEN];
    uint8_t qos;
} sub_t;

typedef struct {
    uint16_t msg_id;
    char     topic[MAX_TOPIC_LEN];
    uint8_t *payload;
    uint32_t payload_len;
    time_t   sent_at;
    bool     active;
} inflight_t;

enum client_state { CS_NEW, CS_CONNECTED, CS_DISCONNECTING };

typedef struct {
    int               fd;
    enum client_state state;
    char              client_id[128];
    uint16_t          keep_alive;
    time_t            last_activity;
    uint8_t           rx_buf[RX_BUF_SIZE];
    uint32_t          rx_len;
    sub_t             subs[MAX_SUBS_PER_CLIENT];
    inflight_t        inflight[MAX_INFLIGHT];
    uint16_t          next_msg_id;
} client_t;

typedef struct {
    char     topic[MAX_TOPIC_LEN];
    uint8_t *payload;
    uint32_t payload_len;
    uint8_t  qos;
} retained_t;

typedef struct {
    int         listen_fd;
    client_t    clients[MAX_CLIENTS];
    retained_t  retained[MAX_RETAINED];
    bool        verbose;
    bool        running;
} broker_t;

/* ---------- mqtt.c — Packet parsing/serialization ---------- */

int mqtt_read_remaining_length(const uint8_t *buf, uint32_t len,
                               uint32_t *value, uint32_t *bytes_consumed);
int mqtt_write_remaining_length(uint8_t *buf, uint32_t value);
int mqtt_read_utf8(const uint8_t *buf, uint32_t len,
                   const char **out_str, uint16_t *out_len);
int mqtt_write_utf8(uint8_t *buf, const char *str, uint16_t str_len);

int mqtt_parse_packet(const uint8_t *buf, uint32_t buflen,
                      uint8_t *pkt_type, uint8_t *flags,
                      const uint8_t **payload, uint32_t *payload_len);

int mqtt_write_connack(uint8_t *buf, uint8_t session_present, uint8_t rc);
int mqtt_write_puback(uint8_t *buf, uint16_t msg_id);
int mqtt_write_suback(uint8_t *buf, uint16_t msg_id,
                      const uint8_t *rcs, int count);
int mqtt_write_unsuback(uint8_t *buf, uint16_t msg_id);
int mqtt_write_pingresp(uint8_t *buf);
int mqtt_write_publish(uint8_t *buf, uint32_t bufsize,
                       const char *topic, const uint8_t *payload,
                       uint32_t plen, uint8_t qos, uint16_t msg_id,
                       bool dup, bool retain);

/* ---------- broker.c — Client management, routing ---------- */

void broker_init(broker_t *b, int port);
void broker_accept(broker_t *b);
void broker_disconnect(broker_t *b, client_t *c);
void broker_handle_packet(broker_t *b, client_t *c,
                          uint8_t pkt_type, uint8_t flags,
                          const uint8_t *data, uint32_t data_len);

bool topic_matches(const char *filter, const char *topic);

void retained_store(broker_t *b, const char *topic,
                    const uint8_t *payload, uint32_t plen, uint8_t qos);
void retained_deliver(broker_t *b, client_t *c,
                      const char *filter, uint8_t sub_qos);

void inflight_send(broker_t *b, client_t *c, const char *topic,
                   const uint8_t *payload, uint32_t plen);
void inflight_ack(client_t *c, uint16_t msg_id);
void inflight_retry(broker_t *b, client_t *c);

#endif /* SEWERPIPE_H */
