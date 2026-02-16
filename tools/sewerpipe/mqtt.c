/*
 * mqtt.c — MQTT 3.1.1 packet parsing and serialization
 */
#include "sewerpipe.h"
#include <string.h>

/* ---------- Wire format helpers ---------- */

int mqtt_read_remaining_length(const uint8_t *buf, uint32_t len,
                               uint32_t *value, uint32_t *bytes_consumed)
{
    uint32_t multiplier = 1;
    uint32_t val = 0;
    uint32_t i = 0;

    do {
        if (i >= len) return 0;          /* incomplete */
        if (i >= 4) return -1;           /* malformed */
        val += (buf[i] & 0x7F) * multiplier;
        multiplier *= 128;
    } while (buf[i++] & 0x80);

    *value = val;
    *bytes_consumed = i;
    return 1;
}

int mqtt_write_remaining_length(uint8_t *buf, uint32_t value)
{
    int n = 0;
    do {
        uint8_t byte = value % 128;
        value /= 128;
        if (value > 0) byte |= 0x80;
        buf[n++] = byte;
    } while (value > 0);
    return n;
}

int mqtt_read_utf8(const uint8_t *buf, uint32_t len,
                   const char **out_str, uint16_t *out_len)
{
    if (len < 2) return -1;
    uint16_t slen = (buf[0] << 8) | buf[1];
    if (len < (uint32_t)(2 + slen)) return -1;
    *out_str = (const char *)(buf + 2);
    *out_len = slen;
    return 2 + slen;
}

int mqtt_write_utf8(uint8_t *buf, const char *str, uint16_t str_len)
{
    buf[0] = (str_len >> 8) & 0xFF;
    buf[1] = str_len & 0xFF;
    memcpy(buf + 2, str, str_len);
    return 2 + str_len;
}

/* ---------- Packet reader ---------- */

int mqtt_parse_packet(const uint8_t *buf, uint32_t buflen,
                      uint8_t *pkt_type, uint8_t *flags,
                      const uint8_t **payload, uint32_t *payload_len)
{
    if (buflen < 2) return 0;  /* need at least fixed header byte + 1 length byte */

    *pkt_type = (buf[0] >> 4) & 0x0F;
    *flags = buf[0] & 0x0F;

    uint32_t rem_len, len_bytes;
    int rc = mqtt_read_remaining_length(buf + 1, buflen - 1, &rem_len, &len_bytes);
    if (rc == 0) return 0;    /* incomplete */
    if (rc < 0) return -1;    /* malformed */

    uint32_t total = 1 + len_bytes + rem_len;
    if (buflen < total) return 0;  /* incomplete */

    *payload = buf + 1 + len_bytes;
    *payload_len = rem_len;
    return (int)total;
}

/* ---------- Packet writers ---------- */

int mqtt_write_connack(uint8_t *buf, uint8_t session_present, uint8_t rc)
{
    buf[0] = (MQTT_CONNACK << 4);
    buf[1] = 2;  /* remaining length */
    buf[2] = session_present & 0x01;
    buf[3] = rc;
    return 4;
}

int mqtt_write_puback(uint8_t *buf, uint16_t msg_id)
{
    buf[0] = (MQTT_PUBACK << 4);
    buf[1] = 2;
    buf[2] = (msg_id >> 8) & 0xFF;
    buf[3] = msg_id & 0xFF;
    return 4;
}

int mqtt_write_suback(uint8_t *buf, uint16_t msg_id,
                      const uint8_t *rcs, int count)
{
    buf[0] = (MQTT_SUBACK << 4);
    int rem = 2 + count;
    int off = 1 + mqtt_write_remaining_length(buf + 1, rem);
    buf[off++] = (msg_id >> 8) & 0xFF;
    buf[off++] = msg_id & 0xFF;
    memcpy(buf + off, rcs, count);
    off += count;
    return off;
}

int mqtt_write_unsuback(uint8_t *buf, uint16_t msg_id)
{
    buf[0] = (MQTT_UNSUBACK << 4);
    buf[1] = 2;
    buf[2] = (msg_id >> 8) & 0xFF;
    buf[3] = msg_id & 0xFF;
    return 4;
}

int mqtt_write_pingresp(uint8_t *buf)
{
    buf[0] = (MQTT_PINGRESP << 4);
    buf[1] = 0;
    return 2;
}

int mqtt_write_publish(uint8_t *buf, uint32_t bufsize,
                       const char *topic, const uint8_t *payload,
                       uint32_t plen, uint8_t qos, uint16_t msg_id,
                       bool dup, bool retain)
{
    uint16_t tlen = (uint16_t)strlen(topic);
    /* variable header: 2 (topic len) + topic + 2 (msg_id if qos>0) */
    uint32_t var_len = 2 + tlen + (qos > 0 ? 2 : 0);
    uint32_t rem_len = var_len + plen;

    /* fixed header: 1 byte type/flags + up to 4 bytes remaining length */
    uint8_t rem_buf[4];
    int rem_bytes = mqtt_write_remaining_length(rem_buf, rem_len);
    uint32_t total = 1 + rem_bytes + rem_len;

    if (total > bufsize) return -1;

    uint8_t flags = 0;
    if (dup) flags |= 0x08;
    if (qos == 1) flags |= 0x02;
    if (retain) flags |= 0x01;
    buf[0] = (MQTT_PUBLISH << 4) | flags;

    int off = 1;
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;

    /* topic */
    buf[off++] = (tlen >> 8) & 0xFF;
    buf[off++] = tlen & 0xFF;
    memcpy(buf + off, topic, tlen);
    off += tlen;

    /* message ID for QoS > 0 */
    if (qos > 0) {
        buf[off++] = (msg_id >> 8) & 0xFF;
        buf[off++] = msg_id & 0xFF;
    }

    /* payload */
    if (plen > 0) {
        memcpy(buf + off, payload, plen);
        off += plen;
    }

    return off;
}
