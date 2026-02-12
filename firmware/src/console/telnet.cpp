#include "telnet.h"

// Telnet protocol bytes
#define IAC   0xFF
#define SB    0xFA
#define SE    0xF0
#define WILL  0xFB
#define WONT  0xFC
#define DO    0xFD
#define DONT  0xFE

#define OPT_ECHO 0x01
#define OPT_SGA  0x03

TelnetServer telnet(23);

TelnetServer::TelnetServer(uint16_t port)
    : server(port), negotiated(false), iac_state(0), iac_cmd(0) {}

void TelnetServer::begin() {
    server.begin();
    server.setNoDelay(true);
}

void TelnetServer::checkClient() {
    if (client && client.connected())
        return;

    // Previous client gone — clean up
    if (client) {
        client.stop();
        negotiated = false;
        iac_state = 0;
    }

    // Accept new connection
    client = server.accept();
    if (client) {
        client.setNoDelay(true);
        negotiate();
    }
}

void TelnetServer::negotiate() {
    // IAC WILL ECHO — server will echo input
    // IAC WILL SGA  — suppress go-ahead (character-at-a-time mode)
    const uint8_t neg[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA };
    client.write(neg, sizeof(neg));
    negotiated = true;
}

size_t TelnetServer::write(uint8_t b) {
    if (!client || !client.connected())
        return 1;   // discard silently (same as TelnetStream2)
    return client.write(b);
}

size_t TelnetServer::write(const uint8_t *buffer, size_t size) {
    if (!client || !client.connected())
        return size;
    return client.write(buffer, size);
}

int TelnetServer::available() {
    if (!client || !client.connected())
        return 0;
    return client.available();
}

int TelnetServer::read() {
    checkClient();

    // Loop to consume IAC sequences, only return data bytes
    for (;;) {
        if (!client || !client.connected())
            return -1;
        if (!client.available())
            return -1;

        uint8_t b = client.read();

        switch (iac_state) {
        case 0: // normal
            if (b == IAC) {
                iac_state = 1;
                break; // consume, loop again
            }
            return b;

        case 1: // got IAC
            if (b == IAC) {
                // IAC IAC = literal 0xFF
                iac_state = 0;
                return 0xFF;
            }
            if (b == SB) {
                iac_state = 3; // subnegotiation
                break;
            }
            // WILL/WONT/DO/DONT + option byte follows
            iac_cmd = b;
            iac_state = 2;
            break;

        case 2: // got IAC + cmd, this byte is the option
            if (iac_cmd == DO) {
                // Client confirms our WILL — good, ignore
            } else if (iac_cmd == DONT) {
                // Client refused — nothing we can do, ignore
            } else if (iac_cmd == WILL) {
                // Client offers something — accept SGA, refuse others
                if (b == OPT_SGA) {
                    const uint8_t resp[] = { IAC, DO, OPT_SGA };
                    client.write(resp, sizeof(resp));
                } else {
                    const uint8_t resp[] = { IAC, DONT, b };
                    client.write(resp, sizeof(resp));
                }
            } else if (iac_cmd == WONT) {
                // Client won't do something — fine, ignore
            }
            iac_state = 0;
            break;

        case 3: // subnegotiation — consume until IAC SE
            if (b == IAC) {
                // Next byte should be SE
                if (client.available()) {
                    uint8_t se = client.read();
                    if (se == SE) {
                        iac_state = 0;
                    }
                    // If not SE, stay in subneg (malformed, but safe)
                }
            }
            break;
        }
    }
}

int TelnetServer::peek() {
    if (!client || !client.connected())
        return -1;
    return client.peek();
}

void TelnetServer::flush() {
    if (client && client.connected())
        client.flush();
}
