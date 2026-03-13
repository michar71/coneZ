/*
 * artnet_receiver.cpp — ArtNet UDP receiver for ConeZ Simulator
 *
 * Uses QUdpSocket in receive mode (readyRead signal on the Qt event loop —
 * no polling thread needed). Applies received DMX data to ledState() using
 * the same (universe, DMX address) mapping as the firmware artnet.cpp.
 */

#include "artnet_receiver.h"
#include "led_state.h"
#include <QNetworkDatagram>
#include <algorithm>

static ArtNetReceiver *s_instance = nullptr;

ArtNetReceiver &ArtNetReceiver::instance()
{
    if (!s_instance)
        s_instance = new ArtNetReceiver();
    return *s_instance;
}

ArtNetReceiver &artnetReceiver() { return ArtNetReceiver::instance(); }

ArtNetReceiver::ArtNetReceiver() : QObject(nullptr)
{
    m_socket = new QUdpSocket(this);
    connect(m_socket, &QUdpSocket::readyRead, this, &ArtNetReceiver::onReadyRead);
}

void ArtNetReceiver::setOutputCallback(std::function<void(const QString &)> cb)
{
    m_outputCb = cb;
}

void ArtNetReceiver::log(const QString &msg)
{
    if (m_outputCb)
        m_outputCb(msg);
}

void ArtNetReceiver::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;

    if (on) {
        if (m_socket->bind(QHostAddress::AnyIPv4, ARTNET_PORT,
                           QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
            log(QString("ArtNet RX: listening on UDP port %1\n").arg(ARTNET_PORT));
        } else {
            log(QString("ArtNet RX: bind failed — %1\n").arg(m_socket->errorString()));
            m_enabled = false;
        }
    } else {
        m_socket->close();
        log("ArtNet RX: disabled\n");
    }
}

void ArtNetReceiver::setUniverseForChannel(int ch, int universe)
{
    if (ch < 1 || ch > 4) return;
    m_uni[ch - 1] = universe;
}

void ArtNetReceiver::setDmxAddrForChannel(int ch, int addr)
{
    if (ch < 1 || ch > 4) return;
    m_dmx[ch - 1] = addr;
}

int ArtNetReceiver::universeForChannel(int ch) const
{
    if (ch < 1 || ch > 4) return 0;
    return m_uni[ch - 1];
}

int ArtNetReceiver::dmxAddrForChannel(int ch) const
{
    if (ch < 1 || ch > 4) return 0;
    return m_dmx[ch - 1];
}

void ArtNetReceiver::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QNetworkDatagram dg = m_socket->receiveDatagram(ARTNET_HEADER + 512);
        QByteArray data = dg.data();
        int n = data.size();

        if (n < ARTNET_HEADER) continue;

        const uint8_t *buf = reinterpret_cast<const uint8_t *>(data.constData());

        if (memcmp(buf, "Art-Net", 8) != 0) continue;
        uint16_t opcode = static_cast<uint16_t>(buf[8] | (buf[9] << 8));
        if (opcode != ARTNET_OPOUTPUT) continue;

        int universe = buf[14] | ((buf[15] & 0x7F) << 8);
        int dmxLen   = (buf[16] << 8) | buf[17];
        if (dmxLen < 2 || dmxLen > 512) continue;
        if (n < ARTNET_HEADER + dmxLen) continue;

        m_rxPackets++;
        applyUniverse(universe, buf + ARTNET_HEADER, dmxLen);
    }
}

void ArtNetReceiver::applyUniverse(int universe, const uint8_t *dmx, int dmxLen)
{
    bool dirty = false;

    for (int ch = 0; ch < 4; ch++) {
        if (m_dmx[ch] == 0)           continue;  // disabled
        if (m_uni[ch] != universe)     continue;  // wrong universe

        int base  = m_dmx[ch] - 1;               // 1-indexed → 0-indexed byte offset
        int count = ledState().count(ch + 1);     // channel is 1-indexed in ledState

        for (int i = 0; i < count; i++) {
            int off = base + i * 3;
            if (off + 2 >= dmxLen) break;
            ledState().setPixel(ch + 1, i, dmx[off], dmx[off + 1], dmx[off + 2]);
        }
        dirty = true;
    }

    if (dirty) {
        ledState().show();
        m_rxFrames++;
    }
}
