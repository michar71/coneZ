/*
 * broker.c — Client management, subscriptions, routing, retained store
 */
#include "sewerpipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>

/* ---------- Helpers ---------- */

static void set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void send_buf(client_t *c, const uint8_t *buf, int len)
{
    if (c->fd < 0 || len <= 0) return;
    int sent = 0;
    while (sent < len) {
        ssize_t n = write(c->fd, buf + sent, len - sent);
        if (n > 0) {
            sent += n;
        } else if (n < 0 && errno == EAGAIN) {
            /* Nonblocking socket full — drop remainder rather than
               spin-waiting.  Acceptable for this broker's scope. */
            break;
        } else {
            break;  /* error or EOF */
        }
    }
}

static time_t now_mono(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec;
}

/* ---------- Topic filter validation ---------- */

/* Check that a subscription filter is well-formed per MQTT 3.1.1:
   - '#' must be the last character and must be preceded by '/' (or be the
     entire filter)
   - '+' must occupy an entire level (preceded by '/' or start, followed by
     '/' or end) */
static bool filter_valid(const char *f)
{
    for (int i = 0; f[i]; i++) {
        if (f[i] == '#') {
            if (f[i + 1] != '\0') return false;            /* not at end */
            if (i > 0 && f[i - 1] != '/') return false;    /* not after / */
        }
        if (f[i] == '+') {
            if (i > 0 && f[i - 1] != '/') return false;    /* not at level start */
            if (f[i + 1] != '\0' && f[i + 1] != '/') return false; /* not at level end */
        }
    }
    return true;
}

/* ---------- Topic matching ---------- */

bool topic_matches(const char *filter, const char *topic)
{
    /* Topics starting with $ don't match wildcard filters at first level */
    if (topic[0] == '$' && (filter[0] == '+' || filter[0] == '#'))
        return false;

    const char *f = filter;
    const char *t = topic;

    while (*f && *t) {
        if (*f == '#') {
            /* # must be last char (possibly after /) — matches everything */
            return true;
        }
        if (*f == '+') {
            /* skip one topic level in topic */
            while (*t && *t != '/') t++;
            f++;
            /* both should be at / or end */
            if (*f == '/' && *t == '/') {
                f++;
                t++;
                continue;
            }
            return (*f == '\0' && *t == '\0');
        }
        if (*f != *t) return false;
        f++;
        t++;
    }

    /* Handle trailing /# in filter */
    if (*f == '/' && *(f + 1) == '#' && *(f + 2) == '\0' && *t == '\0')
        return true;

    /* Handle filter ending with # after we consumed everything */
    if (*f == '#' && *t == '\0')
        return true;

    return (*f == '\0' && *t == '\0');
}

/* ---------- Broker init / accept / disconnect ---------- */

void broker_init(broker_t *b, int port)
{
    memset(b, 0, sizeof(*b));
    b->running = true;

    b->scratch = malloc(RX_BUF_SIZE);
    if (!b->scratch) { perror("malloc"); exit(1); }

    for (int i = 0; i < MAX_CLIENTS; i++)
        b->clients[i].fd = -1;

    b->listen_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (b->listen_fd < 0) {
        /* Fall back to IPv4 */
        b->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (b->listen_fd < 0) {
            perror("socket");
            exit(1);
        }
        int opt = 1;
        setsockopt(b->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = INADDR_ANY;
        addr4.sin_port = htons(port);

        if (bind(b->listen_fd, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
            perror("bind");
            exit(1);
        }
    } else {
        int opt = 1;
        setsockopt(b->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        /* Allow dual-stack (IPv4 + IPv6) */
        int off = 0;
        setsockopt(b->listen_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(port);

        if (bind(b->listen_fd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
            perror("bind");
            exit(1);
        }
    }

    if (listen(b->listen_fd, 16) < 0) {
        perror("listen");
        exit(1);
    }

    set_nonblocking(b->listen_fd);
    printf("sewerpipe: listening on port %d\n", port);
}

void broker_accept(broker_t *b)
{
    struct sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    int fd = accept(b->listen_fd, (struct sockaddr *)&addr, &addrlen);
    if (fd < 0) return;

    /* Find free slot */
    client_t *c = NULL;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (b->clients[i].fd < 0) {
            c = &b->clients[i];
            break;
        }
    }

    if (!c) {
        if (b->verbose)
            fprintf(stderr, "sewerpipe: max clients reached, rejecting\n");
        close(fd);
        return;
    }

    memset(c, 0, sizeof(*c));
    c->fd = fd;
    c->state = CS_NEW;
    c->last_activity = now_mono();
    c->next_msg_id = 1;
    set_nonblocking(fd);

    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    if (b->verbose) {
        char host[64] = "";
        if (addr.ss_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr,
                      host, sizeof(host));
        } else if (addr.ss_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&addr)->sin6_addr,
                      host, sizeof(host));
        }
        printf("sewerpipe: new connection from %s (fd %d)\n", host, fd);
    }
}

void broker_disconnect(broker_t *b, client_t *c)
{
    if (c->fd < 0) return;

    /* Publish will message on unexpected disconnect (MQTT-3.1.2-8) */
    if (c->has_will && c->state == CS_CONNECTED) {
        if (b->verbose)
            printf("sewerpipe: publishing will for '%s': %s\n",
                   c->client_id, c->will_topic);

        if (c->will_retain)
            retained_store(b, c->will_topic, c->will_payload,
                           c->will_payload_len, c->will_qos);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t *sub = &b->clients[i];
            if (sub->fd < 0 || sub->state != CS_CONNECTED) continue;
            if (sub == c) continue;

            for (int j = 0; j < MAX_SUBS_PER_CLIENT; j++) {
                if (sub->subs[j].topic[0] == '\0') continue;
                if (!topic_matches(sub->subs[j].topic, c->will_topic)) continue;

                uint8_t eff_qos = (c->will_qos < sub->subs[j].qos)
                                  ? c->will_qos : sub->subs[j].qos;

                if (eff_qos == 0) {
                    int n = mqtt_write_publish(b->scratch, RX_BUF_SIZE,
                                               c->will_topic, c->will_payload,
                                               c->will_payload_len, 0, 0,
                                               false, false);
                    if (n > 0) send_buf(sub, b->scratch, n);
                } else {
                    inflight_send(b, sub, c->will_topic, c->will_payload,
                                  c->will_payload_len);
                }

                break;  /* one delivery per client */
            }
        }
    }

    if (b->verbose || c->state == CS_CONNECTED)
        printf("sewerpipe: client '%s' disconnected (fd %d)\n",
               c->client_id[0] ? c->client_id : "?", c->fd);

    close(c->fd);

    /* Free inflight payloads */
    for (int i = 0; i < MAX_INFLIGHT; i++) {
        if (c->inflight[i].active) {
            free(c->inflight[i].payload);
            c->inflight[i].active = false;
        }
    }

    free(c->will_payload);

    c->fd = -1;
    c->state = CS_NEW;
    c->client_id[0] = '\0';
    c->rx_len = 0;
    c->has_will = false;
    c->will_payload = NULL;
    c->will_payload_len = 0;
}

/* ---------- Retained message store ---------- */

void retained_store(broker_t *b, const char *topic,
                    const uint8_t *payload, uint32_t plen, uint8_t qos)
{
    /* Find existing entry for this topic */
    retained_t *slot = NULL;
    retained_t *empty = NULL;
    for (int i = 0; i < MAX_RETAINED; i++) {
        if (b->retained[i].topic[0] && strcmp(b->retained[i].topic, topic) == 0) {
            slot = &b->retained[i];
            break;
        }
        if (!empty && b->retained[i].topic[0] == '\0')
            empty = &b->retained[i];
    }

    /* Empty payload = delete retained message */
    if (plen == 0) {
        if (slot) {
            free(slot->payload);
            slot->payload = NULL;
            slot->payload_len = 0;
            slot->topic[0] = '\0';
        }
        return;
    }

    if (!slot) {
        slot = empty;
        if (!slot) {
            fprintf(stderr, "sewerpipe: retained store full, dropping\n");
            return;
        }
    }

    snprintf(slot->topic, MAX_TOPIC_LEN, "%s", topic);
    slot->qos = qos;
    uint8_t *new_payload = realloc(slot->payload, plen);
    if (new_payload) {
        slot->payload = new_payload;
        memcpy(slot->payload, payload, plen);
        slot->payload_len = plen;
    } else {
        /* realloc failed — keep old payload if any, mark slot empty */
        free(slot->payload);
        slot->payload = NULL;
        slot->topic[0] = '\0';
        slot->payload_len = 0;
    }
}

void retained_deliver(broker_t *b, client_t *c,
                      const char *filter, uint8_t sub_qos)
{
    uint8_t *pkt = b->scratch;
    for (int i = 0; i < MAX_RETAINED; i++) {
        retained_t *r = &b->retained[i];
        if (r->topic[0] == '\0') continue;
        if (!topic_matches(filter, r->topic)) continue;

        uint8_t eff_qos = (r->qos < sub_qos) ? r->qos : sub_qos;

        if (eff_qos == 0) {
            int n = mqtt_write_publish(pkt, RX_BUF_SIZE, r->topic,
                                       r->payload, r->payload_len,
                                       0, 0, false, true);
            if (n > 0) send_buf(c, pkt, n);
        } else {
            /* QoS 1 retained: send with retain flag, track in inflight */
            uint16_t mid = c->next_msg_id++;
            if (c->next_msg_id == 0) c->next_msg_id = 1;
            int n = mqtt_write_publish(pkt, RX_BUF_SIZE, r->topic,
                                       r->payload, r->payload_len,
                                       1, mid, false, true);
            if (n > 0) send_buf(c, pkt, n);
            for (int j = 0; j < MAX_INFLIGHT; j++) {
                if (!c->inflight[j].active) {
                    c->inflight[j].active = true;
                    c->inflight[j].msg_id = mid;
                    snprintf(c->inflight[j].topic, MAX_TOPIC_LEN, "%s", r->topic);
                    c->inflight[j].payload = malloc(r->payload_len);
                    if (c->inflight[j].payload)
                        memcpy(c->inflight[j].payload, r->payload, r->payload_len);
                    c->inflight[j].payload_len = r->payload_len;
                    c->inflight[j].sent_at = now_mono();
                    break;
                }
            }
        }
    }
}

/* ---------- QoS 1 inflight management ---------- */

void inflight_send(broker_t *b, client_t *c, const char *topic,
                   const uint8_t *payload, uint32_t plen)
{
    /* Find free inflight slot */
    inflight_t *slot = NULL;
    for (int i = 0; i < MAX_INFLIGHT; i++) {
        if (!c->inflight[i].active) {
            slot = &c->inflight[i];
            break;
        }
    }
    if (!slot) {
        /* Inflight full — drop (or could disconnect) */
        return;
    }

    uint16_t mid = c->next_msg_id++;
    if (c->next_msg_id == 0) c->next_msg_id = 1;

    slot->active = true;
    slot->msg_id = mid;
    snprintf(slot->topic, MAX_TOPIC_LEN, "%s", topic);
    slot->payload = malloc(plen);
    if (slot->payload && plen > 0)
        memcpy(slot->payload, payload, plen);
    slot->payload_len = plen;
    slot->sent_at = now_mono();

    int n = mqtt_write_publish(b->scratch, RX_BUF_SIZE, topic, payload, plen,
                               1, mid, false, false);
    if (n > 0) send_buf(c, b->scratch, n);
}

void inflight_ack(client_t *c, uint16_t msg_id)
{
    for (int i = 0; i < MAX_INFLIGHT; i++) {
        if (c->inflight[i].active && c->inflight[i].msg_id == msg_id) {
            c->inflight[i].active = false;
            free(c->inflight[i].payload);
            c->inflight[i].payload = NULL;
            return;
        }
    }
}

void inflight_retry(broker_t *b, client_t *c)
{
    time_t now = now_mono();

    for (int i = 0; i < MAX_INFLIGHT; i++) {
        inflight_t *inf = &c->inflight[i];
        if (!inf->active) continue;
        if (now - inf->sent_at < RETRY_INTERVAL_SEC) continue;

        int n = mqtt_write_publish(b->scratch, RX_BUF_SIZE, inf->topic,
                                   inf->payload, inf->payload_len,
                                   1, inf->msg_id, true, false);
        if (n > 0) send_buf(c, b->scratch, n);
        inf->sent_at = now;

        if (b->verbose)
            printf("sewerpipe: retry QoS1 msg_id=%u to '%s'\n",
                   inf->msg_id, c->client_id);
    }
}

/* ---------- Packet dispatch ---------- */

static void handle_connect(broker_t *b, client_t *c,
                           const uint8_t *data, uint32_t data_len)
{
    uint8_t pkt[8];

    if (c->state == CS_CONNECTED) {
        /* Already connected — protocol error, disconnect */
        broker_disconnect(b, c);
        return;
    }

    /* Variable header: protocol name (2+4), protocol level (1),
       connect flags (1), keep alive (2) = minimum 10 bytes */
    if (data_len < 10) {
        send_buf(c, pkt, mqtt_write_connack(pkt, 0, CONNACK_UNACCEPTABLE_PROTOCOL));
        broker_disconnect(b, c);
        return;
    }

    /* Protocol name: must be "MQTT" */
    const char *proto_name;
    uint16_t proto_len;
    int consumed = mqtt_read_utf8(data, data_len, &proto_name, &proto_len);
    if (consumed < 0 || proto_len != 4 || memcmp(proto_name, "MQTT", 4) != 0) {
        send_buf(c, pkt, mqtt_write_connack(pkt, 0, CONNACK_UNACCEPTABLE_PROTOCOL));
        broker_disconnect(b, c);
        return;
    }

    uint32_t pos = consumed;

    /* Protocol level: 4 for MQTT 3.1.1 */
    if (pos >= data_len || data[pos] != 4) {
        send_buf(c, pkt, mqtt_write_connack(pkt, 0, CONNACK_UNACCEPTABLE_PROTOCOL));
        broker_disconnect(b, c);
        return;
    }
    pos++;

    /* Connect flags */
    if (pos >= data_len) {
        broker_disconnect(b, c);
        return;
    }
    uint8_t conn_flags = data[pos++];
    /* bool has_username = (conn_flags >> 7) & 1; */
    /* bool has_password = (conn_flags >> 6) & 1; */
    bool will_retain   = (conn_flags >> 5) & 1;
    /* uint8_t will_qos = (conn_flags >> 3) & 3; */
    bool will_flag     = (conn_flags >> 2) & 1;
    bool clean_session = (conn_flags >> 1) & 1;
    if (!clean_session) {
        /* We don't support persistent sessions */
        send_buf(c, pkt, mqtt_write_connack(pkt, 0, CONNACK_IDENTIFIER_REJECTED));
        broker_disconnect(b, c);
        return;
    }

    /* Keep alive */
    if (pos + 2 > data_len) {
        broker_disconnect(b, c);
        return;
    }
    c->keep_alive = (data[pos] << 8) | data[pos + 1];
    pos += 2;

    /* Payload: client ID */
    const char *cid;
    uint16_t cid_len;
    consumed = mqtt_read_utf8(data + pos, data_len - pos, &cid, &cid_len);
    if (consumed < 0) {
        broker_disconnect(b, c);
        return;
    }
    pos += consumed;

    if (cid_len == 0) {
        /* Generate client ID */
        static int gen_counter = 0;
        snprintf(c->client_id, sizeof(c->client_id), "sewerpipe-%d", gen_counter++);
    } else {
        int copy_len = cid_len < (int)sizeof(c->client_id) - 1
                       ? cid_len : (int)sizeof(c->client_id) - 1;
        memcpy(c->client_id, cid, copy_len);
        c->client_id[copy_len] = '\0';
    }

    /* Parse will topic/message if present (MQTT-3.1.2-9) */
    if (will_flag) {
        const char *wt;
        uint16_t wt_len;
        consumed = mqtt_read_utf8(data + pos, data_len - pos, &wt, &wt_len);
        if (consumed < 0) { broker_disconnect(b, c); return; }
        pos += consumed;

        int copy = wt_len < MAX_TOPIC_LEN - 1 ? wt_len : MAX_TOPIC_LEN - 1;
        memcpy(c->will_topic, wt, copy);
        c->will_topic[copy] = '\0';

        const char *wm;
        uint16_t wm_len;
        consumed = mqtt_read_utf8(data + pos, data_len - pos, &wm, &wm_len);
        if (consumed < 0) { broker_disconnect(b, c); return; }
        pos += consumed;

        free(c->will_payload);
        c->will_payload = NULL;
        if (wm_len > 0) {
            c->will_payload = malloc(wm_len);
            if (c->will_payload)
                memcpy(c->will_payload, wm, wm_len);
        }
        c->will_payload_len = wm_len;
        c->will_qos = (conn_flags >> 3) & 3;
        c->will_retain = will_retain;
        c->has_will = true;
    }

    /* Skip username/password — we don't use them */

    /* Check for duplicate client_id — disconnect old client */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *other = &b->clients[i];
        if (other != c && other->fd >= 0 && other->state == CS_CONNECTED &&
            strcmp(other->client_id, c->client_id) == 0) {
            if (b->verbose)
                printf("sewerpipe: duplicate client '%s', disconnecting old\n",
                       c->client_id);
            broker_disconnect(b, other);
        }
    }

    c->state = CS_CONNECTED;
    c->last_activity = now_mono();

    send_buf(c, pkt, mqtt_write_connack(pkt, 0, CONNACK_ACCEPTED));

    printf("sewerpipe: client '%s' connected (fd %d, keepalive %us)\n",
           c->client_id, c->fd, c->keep_alive);
}

static void handle_publish(broker_t *b, client_t *c, uint8_t flags,
                           const uint8_t *data, uint32_t data_len)
{
    bool dup    = (flags >> 3) & 1;
    uint8_t qos = (flags >> 1) & 3;
    bool retain = flags & 1;
    (void)dup;

    if (qos > 1) {
        /* QoS 2 not supported */
        broker_disconnect(b, c);
        return;
    }

    /* Parse topic */
    const char *topic_ptr;
    uint16_t topic_len;
    int consumed = mqtt_read_utf8(data, data_len, &topic_ptr, &topic_len);
    if (consumed < 0 || topic_len == 0 || topic_len >= MAX_TOPIC_LEN) {
        broker_disconnect(b, c);
        return;
    }

    char topic[MAX_TOPIC_LEN];
    memcpy(topic, topic_ptr, topic_len);
    topic[topic_len] = '\0';

    /* MQTT-3.3.2-2: topic must not contain wildcard characters */
    if (memchr(topic, '+', topic_len) || memchr(topic, '#', topic_len)) {
        broker_disconnect(b, c);
        return;
    }

    uint32_t pos = consumed;

    /* Message ID for QoS > 0 */
    uint16_t msg_id = 0;
    if (qos > 0) {
        if (pos + 2 > data_len) {
            broker_disconnect(b, c);
            return;
        }
        msg_id = (data[pos] << 8) | data[pos + 1];
        pos += 2;
    }

    const uint8_t *payload = data + pos;
    uint32_t plen = data_len - pos;

    if (b->verbose)
        printf("sewerpipe: PUBLISH from '%s': %s (%u bytes, qos %u%s)\n",
               c->client_id, topic, plen, qos, retain ? ", retain" : "");

    /* ACK QoS 1 publish from sender */
    if (qos == 1) {
        uint8_t ack[4];
        send_buf(c, ack, mqtt_write_puback(ack, msg_id));
    }

    /* Store retained message */
    if (retain)
        retained_store(b, topic, payload, plen, qos);

    /* Route to subscribers */
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_t *sub = &b->clients[i];
        if (sub->fd < 0 || sub->state != CS_CONNECTED) continue;

        for (int j = 0; j < MAX_SUBS_PER_CLIENT; j++) {
            if (sub->subs[j].topic[0] == '\0') continue;
            if (!topic_matches(sub->subs[j].topic, topic)) continue;

            uint8_t eff_qos = (qos < sub->subs[j].qos) ? qos : sub->subs[j].qos;

            if (eff_qos == 0) {
                int n = mqtt_write_publish(b->scratch, RX_BUF_SIZE, topic,
                                           payload, plen, 0, 0, false, false);
                if (n > 0) send_buf(sub, b->scratch, n);
            } else {
                inflight_send(b, sub, topic, payload, plen);
            }

            break;  /* one delivery per client even if multiple subs match */
        }
    }
}

static void handle_subscribe(broker_t *b, client_t *c,
                             const uint8_t *data, uint32_t data_len)
{
    if (data_len < 2) {
        broker_disconnect(b, c);
        return;
    }

    uint16_t msg_id = (data[0] << 8) | data[1];
    uint32_t pos = 2;

    /* MQTT-3.9.3-1: SUBACK must have one return code per filter.
       We support MAX_SUBS_PER_CLIENT subscriptions; excess get 0x80. */
    #define MAX_SUB_FILTERS 64
    uint8_t rcs[MAX_SUB_FILTERS];
    char filters[MAX_SUB_FILTERS][MAX_TOPIC_LEN];
    int count = 0;

    while (pos < data_len && count < MAX_SUB_FILTERS) {
        const char *filter;
        uint16_t filter_len;
        int consumed = mqtt_read_utf8(data + pos, data_len - pos,
                                      &filter, &filter_len);
        if (consumed < 0 || filter_len == 0) break;
        pos += consumed;

        if (pos >= data_len) break;
        uint8_t req_qos = data[pos++] & 0x03;
        uint8_t granted = (req_qos <= 1) ? req_qos : 1;

        /* Copy filter string */
        int copy = filter_len < MAX_TOPIC_LEN - 1 ? filter_len : MAX_TOPIC_LEN - 1;
        memcpy(filters[count], filter, copy);
        filters[count][copy] = '\0';

        /* Validate filter (reject malformed wildcards) */
        if (!filter_valid(filters[count])) {
            rcs[count++] = 0x80;  /* failure */
            continue;
        }

        /* Find existing or empty slot */
        sub_t *slot = NULL;
        for (int i = 0; i < MAX_SUBS_PER_CLIENT; i++) {
            if (strcmp(c->subs[i].topic, filters[count]) == 0) {
                slot = &c->subs[i];
                break;
            }
        }
        if (!slot) {
            for (int i = 0; i < MAX_SUBS_PER_CLIENT; i++) {
                if (c->subs[i].topic[0] == '\0') {
                    slot = &c->subs[i];
                    break;
                }
            }
        }
        if (slot) {
            memcpy(slot->topic, filters[count], MAX_TOPIC_LEN);
            slot->qos = granted;
        } else {
            granted = 0x80;  /* failure */
        }

        rcs[count++] = granted;

        if (b->verbose)
            printf("sewerpipe: SUBSCRIBE '%s' -> '%s' qos %u\n",
                   c->client_id, filters[count - 1], granted);
    }

    /* Send SUBACK before retained delivery (MQTT-3.8.4) */
    if (count > 0) {
        uint8_t pkt[512];
        int n = mqtt_write_suback(pkt, msg_id, rcs, count);
        if (n > 0) send_buf(c, pkt, n);
    }

    /* Now deliver retained messages for each accepted filter */
    for (int i = 0; i < count; i++) {
        if (rcs[i] <= 1)
            retained_deliver(b, c, filters[i], rcs[i]);
    }
}

static void handle_unsubscribe(broker_t *b, client_t *c,
                               const uint8_t *data, uint32_t data_len)
{
    if (data_len < 2) {
        broker_disconnect(b, c);
        return;
    }

    uint16_t msg_id = (data[0] << 8) | data[1];
    uint32_t pos = 2;

    while (pos < data_len) {
        const char *filter;
        uint16_t filter_len;
        int consumed = mqtt_read_utf8(data + pos, data_len - pos,
                                      &filter, &filter_len);
        if (consumed < 0) break;
        pos += consumed;

        char ftopic[MAX_TOPIC_LEN];
        int copy = filter_len < MAX_TOPIC_LEN - 1 ? filter_len : MAX_TOPIC_LEN - 1;
        memcpy(ftopic, filter, copy);
        ftopic[copy] = '\0';

        for (int i = 0; i < MAX_SUBS_PER_CLIENT; i++) {
            if (strcmp(c->subs[i].topic, ftopic) == 0) {
                c->subs[i].topic[0] = '\0';
                if (b->verbose)
                    printf("sewerpipe: UNSUBSCRIBE '%s' -> '%s'\n",
                           c->client_id, ftopic);
                break;
            }
        }
    }

    uint8_t pkt[4];
    send_buf(c, pkt, mqtt_write_unsuback(pkt, msg_id));
}

void broker_handle_packet(broker_t *b, client_t *c,
                          uint8_t pkt_type, uint8_t flags,
                          const uint8_t *data, uint32_t data_len)
{
    c->last_activity = now_mono();

    /* Only CONNECT allowed before CS_CONNECTED */
    if (c->state != CS_CONNECTED && pkt_type != MQTT_CONNECT) {
        broker_disconnect(b, c);
        return;
    }

    /* Validate reserved flag bits per MQTT 3.1.1 spec */
    switch (pkt_type) {
    case MQTT_SUBSCRIBE:
    case MQTT_UNSUBSCRIBE:
        if (flags != 0x02) {
            broker_disconnect(b, c);
            return;
        }
        break;
    case MQTT_CONNECT:
    case MQTT_PINGREQ:
    case MQTT_DISCONNECT:
    case MQTT_PUBACK:
        if (flags != 0x00) {
            broker_disconnect(b, c);
            return;
        }
        break;
    default:
        break;
    }

    switch (pkt_type) {
    case MQTT_CONNECT:
        handle_connect(b, c, data, data_len);
        break;

    case MQTT_PUBLISH:
        handle_publish(b, c, flags, data, data_len);
        break;

    case MQTT_PUBACK: {
        if (data_len >= 2) {
            uint16_t msg_id = (data[0] << 8) | data[1];
            inflight_ack(c, msg_id);
            if (b->verbose)
                printf("sewerpipe: PUBACK from '%s' msg_id=%u\n",
                       c->client_id, msg_id);
        }
        break;
    }

    case MQTT_SUBSCRIBE:
        handle_subscribe(b, c, data, data_len);
        break;

    case MQTT_UNSUBSCRIBE:
        handle_unsubscribe(b, c, data, data_len);
        break;

    case MQTT_PINGREQ: {
        uint8_t pkt[2];
        send_buf(c, pkt, mqtt_write_pingresp(pkt));
        if (b->verbose)
            printf("sewerpipe: PINGREQ from '%s'\n", c->client_id);
        break;
    }

    case MQTT_DISCONNECT:
        if (b->verbose)
            printf("sewerpipe: DISCONNECT from '%s'\n", c->client_id);
        c->has_will = false;  /* clean disconnect suppresses will */
        broker_disconnect(b, c);
        break;

    default:
        if (b->verbose)
            fprintf(stderr, "sewerpipe: unknown packet type %u from '%s'\n",
                    pkt_type, c->client_id);
        broker_disconnect(b, c);
        break;
    }
}
