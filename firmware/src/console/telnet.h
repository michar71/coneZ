#ifndef TELNET_H
#define TELNET_H

#include <Arduino.h>
#include <WiFi.h>

#define TELNET_MAX_CLIENTS 3

struct TelnetClientSlot {
    WiFiClient client;
    int     iac_state;  // 0=normal, 1=got IAC, 2=got IAC+cmd, 3=subneg
    uint8_t iac_cmd;    // command byte saved from state 1
    bool    needs_prompt; // true after connect, cleared by sendToNew()
};

// Telnet server with IAC negotiation (WILL ECHO + WILL SGA).
// Supports multiple simultaneous clients — input from any client
// feeds the shell, output goes to all connected clients.

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

    bool connected();

    // Write to newly-connected clients only; clearNewClients() resets flags
    bool hasNewClients();
    size_t sendToNew(const uint8_t *buffer, size_t size);
    size_t sendToNew(const char *s) { return sendToNew((const uint8_t *)s, strlen(s)); }
    void   clearNewClients();

private:
    WiFiServer server;
    TelnetClientSlot clients[TELNET_MAX_CLIENTS];
    bool prev_was_cr;   // for \n → \r\n translation across write calls

    void checkClient();
    void negotiate(TelnetClientSlot &slot);
};

extern TelnetServer telnet;

#endif
