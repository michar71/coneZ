/*
 * artnet_sender.cpp — ArtNet (Art-Net) UDP output for ConeZ Simulator
 *
 * Art-Net protocol: UDP port 6454, OpOutput (0x5000) packets.
 * Each universe carries up to 512 bytes (170 RGB pixels).
 * All 4 LED channels are packed sequentially into consecutive universes.
 */

#include "artnet_sender.h"
#include "led_state.h"
#include <cstring>

static ArtNetSender *s_instance = nullptr;

ArtNetSender &ArtNetSender::instance()
{
    if (!s_instance)
        s_instance = new ArtNetSender();
    return *s_instance;
}

ArtNetSender &artnetSender() { return ArtNetSender::instance(); }

ArtNetSender::ArtNetSender()
    : QObject(nullptr)
{
    m_socket = new QUdpSocket(this);
    m_destAddr = QHostAddress(m_destHost);
}

void ArtNetSender::setOutputCallback(std::function<void(const QString&)> cb)
{
    m_outputCb = cb;
}

void ArtNetSender::log(const QString &msg)
{
    if (m_outputCb)
        m_outputCb(msg);
}

void ArtNetSender::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    if (on) {
        log(QString("ArtNet: enabled, sending to %1:%2 universe %3\n")
            .arg(m_destHost).arg(m_destPort).arg(m_universeOffset));
    } else {
        log("ArtNet: disabled\n");
    }
}

bool ArtNetSender::enabled() const { return m_enabled; }

void ArtNetSender::setDestination(const QString &host, int port)
{
    m_destHost = host;
    m_destPort = port;
    m_destAddr = QHostAddress(host);
}

QString ArtNetSender::host() const { return m_destHost; }
int ArtNetSender::port() const { return m_destPort; }

void ArtNetSender::setUniverse(int offset)
{
    m_universeOffset = offset;
}

int ArtNetSender::universe() const { return m_universeOffset; }

void ArtNetSender::sendFrame(const std::vector<std::vector<RGB>> &channels)
{
    if (!m_enabled) return;

    // Flatten all channels into a contiguous RGB byte stream
    int totalPixels = 0;
    for (const auto &ch : channels)
        totalPixels += (int)ch.size();

    if (totalPixels == 0) return;

    int totalBytes = totalPixels * 3;
    std::vector<uint8_t> flat(totalBytes);
    int offset = 0;
    for (const auto &ch : channels) {
        for (const auto &px : ch) {
            flat[offset++] = px.r;
            flat[offset++] = px.g;
            flat[offset++] = px.b;
        }
    }

    // Split into universes and send
    int bytesSent = 0;
    int universeNum = 0;
    while (bytesSent < totalBytes) {
        int remaining = totalBytes - bytesSent;
        int chunkSize = (remaining > DMX_UNIVERSE_SIZE) ? DMX_UNIVERSE_SIZE : remaining;
        // ArtNet length must be even
        int sendLen = (chunkSize + 1) & ~1;
        sendUniverse(m_universeOffset + universeNum, flat.data() + bytesSent, sendLen);
        bytesSent += chunkSize;
        universeNum++;
    }

    // Advance sequence (1-255, skip 0)
    m_sequence++;
    if (m_sequence == 0) m_sequence = 1;

    m_frameCount++;
}

void ArtNetSender::sendUniverse(int universeNum, const uint8_t *data, int len)
{
    uint8_t packet[ARTNET_HEADER_SIZE + DMX_UNIVERSE_SIZE];
    memset(packet, 0, sizeof(packet));

    // Art-Net header
    memcpy(packet, "Art-Net", 8);           // ID (8 bytes, null-terminated)
    packet[8] = 0x00;                        // OpCode low  (OpOutput = 0x5000)
    packet[9] = 0x50;                        // OpCode high
    packet[10] = 0;                          // ProtVerHi (14)
    packet[11] = 14;                         // ProtVerLo
    packet[12] = m_sequence;                 // Sequence
    packet[13] = 0;                          // Physical
    packet[14] = universeNum & 0xFF;         // SubUni (universe low)
    packet[15] = (universeNum >> 8) & 0x7F;  // Net (universe high)
    packet[16] = (len >> 8) & 0xFF;          // LengthHi
    packet[17] = len & 0xFF;                 // LengthLo

    // DMX data
    if (len > DMX_UNIVERSE_SIZE) len = DMX_UNIVERSE_SIZE;
    memcpy(packet + ARTNET_HEADER_SIZE, data, len);

    int packetSize = ARTNET_HEADER_SIZE + ((len + 1) & ~1);  // pad to even
    m_socket->writeDatagram((const char *)packet, packetSize, m_destAddr, m_destPort);
    m_packetCount++;
}
