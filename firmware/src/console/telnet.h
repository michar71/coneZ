#ifndef TELNET_H
#define TELNET_H

#include <Arduino.h>
#include <WiFi.h>

// Telnet server with IAC negotiation (WILL ECHO + WILL SGA).
// Replaces TelnetStream2 with proper protocol handling so that
// Telnet clients disable local echo and enter character mode.

class TelnetServer : public Stream {
public:
    TelnetServer(uint16_t port = 23);

    void begin();

    // Stream interface
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    int    available() override;
    int    read() override;
    int    peek() override;
    void   flush() override;

    bool connected() { return client && client.connected(); }

private:
    WiFiServer server;
    WiFiClient client;
    bool   negotiated;
    int    iac_state;   // 0=normal, 1=got IAC, 2=got IAC+cmd, 3=subneg
    uint8_t iac_cmd;    // command byte saved from state 1

    void checkClient();
    void negotiate();
};

extern TelnetServer telnet;

#endif
