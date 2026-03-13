/*
 * artnet_receiver.h — ArtNet UDP receiver for ConeZ Simulator
 *
 * Receives Art-Net OpOutput packets on UDP port 6454 and writes DMX data
 * into the LED state buffers, mirroring the firmware artnet.cpp behaviour.
 * Off by default — enable via CLI "artnet rx enable" or --artnet-rx flag.
 */

#ifndef SIM_ARTNET_RECEIVER_H
#define SIM_ARTNET_RECEIVER_H

#include <QObject>
#include <QUdpSocket>
#include <QString>
#include <functional>
#include <cstdint>

class ArtNetReceiver : public QObject {
    Q_OBJECT
public:
    static ArtNetReceiver &instance();

    void setOutputCallback(std::function<void(const QString &)> cb);

    void setEnabled(bool on);
    bool enabled() const { return m_enabled; }

    // Per-channel mapping: universe and DMX start address (1-indexed; 0 = disabled)
    void setUniverseForChannel(int ch, int universe);  // ch 1-4
    void setDmxAddrForChannel(int ch, int addr);       // ch 1-4, addr 1-512 or 0

    int universeForChannel(int ch) const;
    int dmxAddrForChannel(int ch) const;

    uint32_t rxPackets() const { return m_rxPackets; }
    uint32_t rxFrames()  const { return m_rxFrames; }

private slots:
    void onReadyRead();

private:
    ArtNetReceiver();

    void applyUniverse(int universe, const uint8_t *dmx, int dmxLen);
    void log(const QString &msg);

    std::function<void(const QString &)> m_outputCb;

    QUdpSocket *m_socket = nullptr;
    bool m_enabled = false;

    int m_uni[4] = {0, 0, 0, 0};
    int m_dmx[4] = {1, 0, 0, 0};   // dmx[N]=0 means channel N+1 disabled

    uint32_t m_rxPackets = 0;
    uint32_t m_rxFrames  = 0;

    static constexpr int ARTNET_PORT   = 6454;
    static constexpr int ARTNET_HEADER = 18;
    static constexpr uint16_t ARTNET_OPOUTPUT = 0x5000;
};

ArtNetReceiver &artnetReceiver();

#endif
