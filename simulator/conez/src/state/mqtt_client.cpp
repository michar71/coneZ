/*
 * mqtt_client.cpp — Qt-based MQTT 3.1.1 client for ConeZ Simulator
 *
 * Lightweight MQTT client using QTcpSocket, ported from firmware mqtt_client.cpp.
 * State machine: DISCONNECTED → WAIT_CONNACK → CONNECTED
 * Auto-reconnect with exponential backoff (1s → 30s cap).
 */

#include "mqtt_client.h"
#include "sim_config.h"
#include <QDateTime>
#include <cstring>

// MQTT 3.1.1 packet types
#define MQTT_CONNECT      1
#define MQTT_CONNACK      2
#define MQTT_PUBLISH      3
#define MQTT_SUBSCRIBE    8
#define MQTT_SUBACK       9
#define MQTT_PINGREQ     12
#define MQTT_PINGRESP    13
#define MQTT_DISCONNECT  14

// ---------- Singleton ----------

static SimMqttClient *s_instance = nullptr;

SimMqttClient &SimMqttClient::instance()
{
    if (!s_instance)
        s_instance = new SimMqttClient();
    return *s_instance;
}

SimMqttClient &mqttClient()
{
    return SimMqttClient::instance();
}

// ---------- Constructor ----------

SimMqttClient::SimMqttClient()
    : QObject(nullptr)
{
    m_socket = new QTcpSocket(this);

    connect(m_socket, &QTcpSocket::connected, this, &SimMqttClient::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &SimMqttClient::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &SimMqttClient::onSocketReadyRead);
    connect(m_socket, &QAbstractSocket::errorOccurred, this, &SimMqttClient::onSocketError);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SimMqttClient::onReconnectTimer);

    m_connackTimer = new QTimer(this);
    m_connackTimer->setSingleShot(true);
    connect(m_connackTimer, &QTimer::timeout, this, &SimMqttClient::onConnackTimeout);

    m_pingTimer = new QTimer(this);
    connect(m_pingTimer, &QTimer::timeout, this, &SimMqttClient::onPingTimer);

    m_heartbeatTimer = new QTimer(this);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &SimMqttClient::onHeartbeatTimer);

    // Build topic strings from cone_id
    int coneId = simConfig().cone_id;
    m_clientId = QString("conez-%1").arg(coneId);
    m_topicStatus = QString("conez/%1/status").arg(coneId);
    m_topicCmd = QString("conez/%1/cmd/#").arg(coneId);
}

// ---------- Output ----------

void SimMqttClient::setOutputCallback(std::function<void(const QString&)> cb)
{
    m_outputCb = cb;
}

void SimMqttClient::log(const QString &msg)
{
    if (m_outputCb)
        m_outputCb("[MQTT] " + msg + "\n");
}

// ---------- Config ----------

void SimMqttClient::setBroker(const QString &host, int port)
{
    m_broker = host;
    m_port = port;
}

QString SimMqttClient::broker() const { return m_broker; }
int SimMqttClient::port() const { return m_port; }

void SimMqttClient::setEnabled(bool on)
{
    m_enabled = on;
    if (on && m_state == ST_DISCONNECTED && !m_userDisconnected) {
        // Start connection attempt
        m_reconnectDelay = 0;
        onReconnectTimer();
    } else if (!on && m_state != ST_DISCONNECTED) {
        disconnectFromBroker();
    }
}

bool SimMqttClient::enabled() const { return m_enabled; }

// ---------- Control ----------

void SimMqttClient::connectToBroker()
{
    m_userDisconnected = false;
    m_enabled = true;
    if (m_state == ST_DISCONNECTED) {
        m_reconnectTimer->stop();
        m_reconnectDelay = 0;
        onReconnectTimer();
    }
}

void SimMqttClient::disconnectFromBroker()
{
    m_userDisconnected = true;
    m_reconnectTimer->stop();
    m_connackTimer->stop();
    m_pingTimer->stop();
    m_heartbeatTimer->stop();

    if (m_state != ST_DISCONNECTED) {
        if (m_socket->state() == QAbstractSocket::ConnectedState) {
            uint8_t buf[2];
            int len = buildDisconnect(buf);
            mqttSend(buf, len);
        }
        m_socket->abort();
        m_state = ST_DISCONNECTED;
        m_rxBuf.clear();
        log("Disconnected");
    }
}

int SimMqttClient::publish(const QString &topic, const QByteArray &payload)
{
    if (m_state != ST_CONNECTED) return -1;

    uint8_t buf[BUF_SIZE];
    QByteArray topicUtf8 = topic.toUtf8();
    int len = buildPublish(buf, BUF_SIZE, topicUtf8.constData(), payload.constData(), false);
    if (len <= 0) return -1;

    return mqttSend(buf, len) ? 0 : -1;
}

// ---------- Status ----------

bool SimMqttClient::connected() const { return m_state == ST_CONNECTED; }

QString SimMqttClient::stateStr() const
{
    switch (m_state) {
    case ST_DISCONNECTED:  return "Disconnected";
    case ST_WAIT_CONNACK:  return "Waiting for CONNACK";
    case ST_CONNECTED:     return "Connected";
    default:               return "Unknown";
    }
}

uint32_t SimMqttClient::uptimeSec() const
{
    if (m_state != ST_CONNECTED) return 0;
    return (uint32_t)((QDateTime::currentMSecsSinceEpoch() - m_connectedAtMs) / 1000);
}

uint32_t SimMqttClient::txCount() const { return m_txCount; }
uint32_t SimMqttClient::rxCount() const { return m_rxCount; }

// ---------- Wire format helpers ----------

int SimMqttClient::writeRemainingLength(uint8_t *buf, uint32_t value)
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

int SimMqttClient::readRemainingLength(const uint8_t *buf, int len,
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

int SimMqttClient::writeUtf8(uint8_t *buf, const char *str)
{
    uint16_t slen = (uint16_t)strlen(str);
    buf[0] = (slen >> 8) & 0xFF;
    buf[1] = slen & 0xFF;
    memcpy(buf + 2, str, slen);
    return 2 + slen;
}

// ---------- Packet builders ----------

int SimMqttClient::buildConnect(uint8_t *buf, int bufsize)
{
    uint8_t body[128];
    int p = 0;

    // Protocol name "MQTT"
    p += writeUtf8(body + p, "MQTT");
    // Protocol level 4 (MQTT 3.1.1)
    body[p++] = 0x04;
    // Connect flags: clean session
    body[p++] = 0x02;
    // Keep alive
    body[p++] = (KEEPALIVE_SEC >> 8) & 0xFF;
    body[p++] = KEEPALIVE_SEC & 0xFF;
    // Payload: client ID
    QByteArray cid = m_clientId.toUtf8();
    p += writeUtf8(body + p, cid.constData());

    // Fixed header
    uint8_t rem_buf[4];
    int rem_bytes = writeRemainingLength(rem_buf, p);
    int total = 1 + rem_bytes + p;
    if (total > bufsize) return -1;

    int off = 0;
    buf[off++] = (MQTT_CONNECT << 4);
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;
    memcpy(buf + off, body, p);

    return off + p;
}

int SimMqttClient::buildSubscribe(uint8_t *buf, int bufsize, const char *filter, uint8_t qos)
{
    uint8_t body[128];
    int p = 0;

    uint16_t mid = m_nextMsgId++;
    if (m_nextMsgId == 0) m_nextMsgId = 1;
    body[p++] = (mid >> 8) & 0xFF;
    body[p++] = mid & 0xFF;

    p += writeUtf8(body + p, filter);
    body[p++] = qos;

    uint8_t rem_buf[4];
    int rem_bytes = writeRemainingLength(rem_buf, p);
    int total = 1 + rem_bytes + p;
    if (total > bufsize) return -1;

    int off = 0;
    buf[off++] = (MQTT_SUBSCRIBE << 4) | 0x02;
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;
    memcpy(buf + off, body, p);

    return off + p;
}

int SimMqttClient::buildPublish(uint8_t *buf, int bufsize,
                                const char *topic, const char *payload, bool retain)
{
    uint16_t tlen = (uint16_t)strlen(topic);
    uint32_t plen = (uint32_t)strlen(payload);
    uint32_t rem_len = 2 + tlen + plen;

    uint8_t rem_buf[4];
    int rem_bytes = writeRemainingLength(rem_buf, rem_len);
    int total = 1 + rem_bytes + rem_len;
    if (total > bufsize) return -1;

    int off = 0;
    uint8_t flags = 0;
    if (retain) flags |= 0x01;
    buf[off++] = (MQTT_PUBLISH << 4) | flags;
    memcpy(buf + off, rem_buf, rem_bytes);
    off += rem_bytes;

    buf[off++] = (tlen >> 8) & 0xFF;
    buf[off++] = tlen & 0xFF;
    memcpy(buf + off, topic, tlen);
    off += tlen;

    if (plen > 0) {
        memcpy(buf + off, payload, plen);
        off += plen;
    }

    return off;
}

int SimMqttClient::buildPingreq(uint8_t *buf)
{
    buf[0] = (MQTT_PINGREQ << 4);
    buf[1] = 0x00;
    return 2;
}

int SimMqttClient::buildDisconnect(uint8_t *buf)
{
    buf[0] = (MQTT_DISCONNECT << 4);
    buf[1] = 0x00;
    return 2;
}

// ---------- Send ----------

bool SimMqttClient::mqttSend(const uint8_t *buf, int len)
{
    if (len <= 0) return false;
    if (m_socket->state() != QAbstractSocket::ConnectedState) return false;

    qint64 written = m_socket->write(reinterpret_cast<const char*>(buf), len);
    if (written == len) {
        m_txCount++;
        return true;
    }
    log("Partial write, disconnecting");
    m_socket->abort();
    m_state = ST_DISCONNECTED;
    m_rxBuf.clear();
    return false;
}

// ---------- Incoming handlers ----------

void SimMqttClient::handleConnack(const uint8_t *payload, uint32_t plen)
{
    if (plen < 2) {
        log("CONNACK too short");
        return;
    }

    uint8_t rc = payload[1];
    if (rc != 0) {
        log(QString("CONNACK rejected (rc=%1)").arg(rc));
        m_socket->abort();
        m_state = ST_DISCONNECTED;
        return;
    }

    m_connackTimer->stop();
    m_state = ST_CONNECTED;
    m_connectedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_lastPingrespMs = m_connectedAtMs;
    m_reconnectDelay = RECONNECT_INIT;
    m_userDisconnected = false;

    log(QString("Connected to %1:%2").arg(m_broker).arg(m_port));

    // Subscribe to command topic
    uint8_t buf[BUF_SIZE];
    QByteArray topicUtf8 = m_topicCmd.toUtf8();
    int len = buildSubscribe(buf, BUF_SIZE, topicUtf8.constData(), 0);
    if (len > 0) mqttSend(buf, len);

    // Start ping and heartbeat timers
    m_pingTimer->start(KEEPALIVE_SEC * 500);
    m_heartbeatTimer->start(30000);
}

void SimMqttClient::handlePublish(uint8_t flags, const uint8_t *data, uint32_t dlen)
{
    if (dlen < 2) return;

    uint8_t qos = (flags >> 1) & 3;

    uint16_t tlen = (data[0] << 8) | data[1];
    if (2u + tlen > dlen) return;

    QString topic = QString::fromUtf8(reinterpret_cast<const char*>(data + 2), tlen);

    uint32_t pos = 2 + tlen;
    if (qos > 0) {
        if (pos + 2 > dlen) return;
        pos += 2;
    }

    QString payload = QString::fromUtf8(reinterpret_cast<const char*>(data + pos), dlen - pos);

    m_rxCount++;
    log(QString("RX [%1] %2").arg(topic, payload));
}

void SimMqttClient::handleSuback(const uint8_t *data, uint32_t dlen)
{
    if (dlen < 3) return;
    uint8_t rc = data[2];
    if (rc == 0x80) {
        log("SUBACK: subscription rejected");
    } else {
        log(QString("Subscribed to %1 (qos=%2)").arg(m_topicCmd).arg(rc));
    }
}

int SimMqttClient::parseAndDispatch()
{
    if (m_rxBuf.size() < 2) return 0;

    const uint8_t *raw = reinterpret_cast<const uint8_t*>(m_rxBuf.constData());
    int rawLen = m_rxBuf.size();

    uint8_t pkt_type = (raw[0] >> 4) & 0x0F;
    uint8_t flags = raw[0] & 0x0F;

    uint32_t rem_len;
    int len_bytes;
    int rc = readRemainingLength(raw + 1, rawLen - 1, &rem_len, &len_bytes);
    if (rc == 0) return 0;
    if (rc < 0) return -1;

    uint32_t total = 1 + len_bytes + rem_len;
    if ((uint32_t)rawLen < total) return 0;

    const uint8_t *payload = raw + 1 + len_bytes;

    switch (pkt_type) {
    case MQTT_CONNACK:
        handleConnack(payload, rem_len);
        break;
    case MQTT_PUBLISH:
        handlePublish(flags, payload, rem_len);
        break;
    case MQTT_SUBACK:
        handleSuback(payload, rem_len);
        break;
    case MQTT_PINGRESP:
        m_lastPingrespMs = QDateTime::currentMSecsSinceEpoch();
        break;
    default:
        log(QString("Unknown packet type %1").arg(pkt_type));
        break;
    }

    return (int)total;
}

// ---------- Socket slots ----------

void SimMqttClient::onSocketConnected()
{
    // TCP connected — send MQTT CONNECT packet
    uint8_t buf[BUF_SIZE];
    int len = buildConnect(buf, BUF_SIZE);
    if (len <= 0 || !mqttSend(buf, len)) {
        m_socket->abort();
        m_state = ST_DISCONNECTED;
        return;
    }
    m_state = ST_WAIT_CONNACK;
    m_connackTimer->start(CONNACK_TIMEOUT_MS);
}

void SimMqttClient::onSocketDisconnected()
{
    bool wasConnected = (m_state == ST_CONNECTED);
    m_state = ST_DISCONNECTED;
    m_rxBuf.clear();
    m_connackTimer->stop();
    m_pingTimer->stop();
    m_heartbeatTimer->stop();

    if (wasConnected)
        log("Connection lost");

    scheduleReconnect();
}

void SimMqttClient::onSocketReadyRead()
{
    m_rxBuf.append(m_socket->readAll());

    for (;;) {
        int consumed = parseAndDispatch();
        if (consumed <= 0) {
            if (consumed < 0) {
                log("Protocol error, disconnecting");
                m_socket->abort();
                m_state = ST_DISCONNECTED;
                m_rxBuf.clear();
            }
            break;
        }
        m_rxBuf.remove(0, consumed);
    }
}

void SimMqttClient::onSocketError(QAbstractSocket::SocketError err)
{
    Q_UNUSED(err);
    // For immediate-fail errors (e.g. ConnectionRefused), Qt may emit
    // errorOccurred without a subsequent disconnected signal if the socket
    // was never in ConnectedState.  Only schedule reconnect here for that
    // case; otherwise onSocketDisconnected() handles it.
    if (m_socket->state() == QAbstractSocket::UnconnectedState &&
        m_state == ST_DISCONNECTED) {
        log(QString("TCP connect failed (%1)").arg(m_socket->errorString()));
        scheduleReconnect();
    }
}

// ---------- Reconnect helper ----------

void SimMqttClient::scheduleReconnect()
{
    if (!m_enabled || m_userDisconnected) return;
    if (m_reconnectTimer->isActive()) return;  // already scheduled

    if (m_reconnectDelay < RECONNECT_INIT)
        m_reconnectDelay = RECONNECT_INIT;
    m_reconnectTimer->start(m_reconnectDelay);
    m_reconnectDelay = qMin(m_reconnectDelay * 2, RECONNECT_MAX);
}

// ---------- Timer slots ----------

void SimMqttClient::onReconnectTimer()
{
    if (!m_enabled || m_userDisconnected) return;
    if (m_state != ST_DISCONNECTED) return;
    if (m_broker.isEmpty()) return;

    log(QString("Connecting to %1:%2...").arg(m_broker).arg(m_port));
    m_socket->connectToHost(m_broker, m_port);
}

void SimMqttClient::onConnackTimeout()
{
    if (m_state != ST_WAIT_CONNACK) return;
    log("CONNACK timeout, disconnecting");
    m_socket->abort();
    m_state = ST_DISCONNECTED;
    m_rxBuf.clear();
    scheduleReconnect();
}

void SimMqttClient::onPingTimer()
{
    if (m_state != ST_CONNECTED) return;

    // Check PINGRESP timeout — broker stopped responding
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if ((now - m_lastPingrespMs) > PINGRESP_TIMEOUT_MS) {
        log("PINGRESP timeout, disconnecting");
        m_socket->abort();
        m_state = ST_DISCONNECTED;
        m_rxBuf.clear();
        m_pingTimer->stop();
        m_heartbeatTimer->stop();
        scheduleReconnect();
        return;
    }

    uint8_t buf[2];
    int len = buildPingreq(buf);
    mqttSend(buf, len);
}

void SimMqttClient::onHeartbeatTimer()
{
    if (m_state != ST_CONNECTED) return;

    auto &cfg = simConfig();
    auto uptime = std::chrono::steady_clock::now() - cfg.start_time;
    auto uptimeSec = std::chrono::duration_cast<std::chrono::seconds>(uptime).count();

    QString payload = QString("{\"uptime\":%1,\"sim\":true}").arg(uptimeSec);
    QByteArray topicUtf8 = m_topicStatus.toUtf8();

    uint8_t buf[BUF_SIZE];
    int len = buildPublish(buf, BUF_SIZE, topicUtf8.constData(),
                           payload.toUtf8().constData(), false);
    if (len > 0) mqttSend(buf, len);
}
