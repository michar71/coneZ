/*
 * mqtt_client.h — Qt-based MQTT 3.1.1 client for ConeZ Simulator
 *
 * Mirrors firmware mqtt_client API. Uses QTcpSocket for networking.
 * Singleton pattern matching CueEngine / SimConfig.
 */

#ifndef SIM_MQTT_CLIENT_H
#define SIM_MQTT_CLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QString>
#include <QByteArray>
#include <functional>
#include <cstdint>

class SimMqttClient : public QObject {
    Q_OBJECT
public:
    static SimMqttClient &instance();

    void setOutputCallback(std::function<void(const QString&)> cb);

    // Config
    void setBroker(const QString &host, int port);
    QString broker() const;
    int port() const;

    // Control
    void setEnabled(bool on);
    bool enabled() const;
    void connectToBroker();
    void disconnectFromBroker();
    int  publish(const QString &topic, const QByteArray &payload);

    // Status
    bool connected() const;
    QString stateStr() const;
    uint32_t uptimeSec() const;
    uint32_t txCount() const;
    uint32_t rxCount() const;

private:
    SimMqttClient();

    enum State { ST_DISCONNECTED, ST_WAIT_CONNACK, ST_CONNECTED };

    // Wire format helpers
    static int writeRemainingLength(uint8_t *buf, uint32_t value);
    static int readRemainingLength(const uint8_t *buf, int len,
                                   uint32_t *value, int *consumed);
    static int writeUtf8(uint8_t *buf, const char *str);

    // Packet builders
    int buildConnect(uint8_t *buf, int bufsize);
    int buildSubscribe(uint8_t *buf, int bufsize, const char *filter, uint8_t qos);
    static int buildPublish(uint8_t *buf, int bufsize,
                            const char *topic, const char *payload, bool retain);
    static int buildPingreq(uint8_t *buf);
    static int buildDisconnect(uint8_t *buf);

    // Send
    bool mqttSend(const uint8_t *buf, int len);

    // Reconnect scheduling (single point of truth for backoff)
    void scheduleReconnect();

    // Incoming handlers
    void handleConnack(const uint8_t *payload, uint32_t plen);
    void handlePublish(uint8_t flags, const uint8_t *data, uint32_t dlen);
    void handleSuback(const uint8_t *data, uint32_t dlen);
    int  parseAndDispatch();

    // Output
    void log(const QString &msg);

    std::function<void(const QString&)> m_outputCb;

    QTcpSocket *m_socket;
    State m_state = ST_DISCONNECTED;

    QString m_broker = "localhost";
    int m_port = 1883;
    bool m_enabled = false;
    bool m_userDisconnected = false;

    // Timers
    QTimer *m_reconnectTimer;
    QTimer *m_connackTimer;
    QTimer *m_pingTimer;
    QTimer *m_heartbeatTimer;
    uint32_t m_reconnectDelay = 1000;
    qint64 m_lastPingrespMs = 0;

    // Packet state
    uint16_t m_nextMsgId = 1;
    QByteArray m_rxBuf;

    // Stats
    qint64 m_connectedAtMs = 0;
    uint32_t m_txCount = 0;
    uint32_t m_rxCount = 0;

    // Topic strings
    QString m_clientId;
    QString m_topicStatus;
    QString m_topicCmd;

    static constexpr uint32_t RECONNECT_INIT = 1000;
    static constexpr uint32_t RECONNECT_MAX  = 30000;
    static constexpr int KEEPALIVE_SEC = 60;
    static constexpr int CONNACK_TIMEOUT_MS = 5000;
    static constexpr int PINGRESP_TIMEOUT_MS = KEEPALIVE_SEC * 1500; // 1.5x keepalive
    static constexpr int BUF_SIZE = 512;

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onReconnectTimer();
    void onConnackTimeout();
    void onPingTimer();
    void onHeartbeatTimer();
};

SimMqttClient &mqttClient();

#endif
