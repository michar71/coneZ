/*
 * artnet_sender.h — ArtNet (Art-Net) UDP output for ConeZ Simulator
 *
 * Sends LED pixel data as ArtNet DMX packets over UDP.
 * Off by default — enable via CLI "artnet enable" or --artnet flag.
 * Singleton pattern matching SimMqttClient / CueEngine.
 */

#ifndef SIM_ARTNET_SENDER_H
#define SIM_ARTNET_SENDER_H

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QString>
#include <functional>
#include <vector>
#include <cstdint>

struct RGB;

class ArtNetSender : public QObject {
    Q_OBJECT
public:
    static ArtNetSender &instance();

    void setOutputCallback(std::function<void(const QString&)> cb);

    // Config
    void setEnabled(bool on);
    bool enabled() const;
    void setDestination(const QString &host, int port = 6454);
    QString host() const;
    int port() const;
    void setUniverse(int offset);
    int universe() const;

    // Send current frame — called from LedStripWidget::refresh()
    void sendFrame(const std::vector<std::vector<RGB>> &channels);

    // Stats
    uint32_t frameCount() const { return m_frameCount; }
    uint32_t packetCount() const { return m_packetCount; }

private:
    ArtNetSender();

    void sendUniverse(int universeNum, const uint8_t *data, int len);
    void log(const QString &msg);

    std::function<void(const QString&)> m_outputCb;

    QUdpSocket *m_socket;
    QHostAddress m_destAddr;
    QString m_destHost = "255.255.255.255";
    int m_destPort = 6454;
    int m_universeOffset = 0;
    bool m_enabled = false;
    uint8_t m_sequence = 1;  // 1-255, skips 0

    uint32_t m_frameCount = 0;
    uint32_t m_packetCount = 0;

    static constexpr int ARTNET_HEADER_SIZE = 18;
    static constexpr int DMX_UNIVERSE_SIZE = 512;
    static constexpr int MAX_PIXELS_PER_UNIVERSE = 170;  // 512 / 3
};

ArtNetSender &artnetSender();

#endif
