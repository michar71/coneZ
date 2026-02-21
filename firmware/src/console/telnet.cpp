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
    : server(port), prev_was_cr(false) {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        clients[i].iac_state = 0;
        clients[i].iac_cmd = 0;
        clients[i].needs_prompt = false;
    }
}

void TelnetServer::begin() {
    server.begin();
    server.setNoDelay(true);
}

void TelnetServer::checkClient() {
    // Clean up disconnected slots
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && !clients[i].client.connected()) {
            clients[i].client.stop();
            clients[i].iac_state = 0;
            clients[i].iac_cmd = 0;
        }
    }

    // Accept new connection into first free slot
    WiFiClient incoming = server.accept();
    if (incoming) {
        for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
            if (!clients[i].client || !clients[i].client.connected()) {
                clients[i].client = incoming;
                clients[i].iac_state = 0;
                clients[i].iac_cmd = 0;
                clients[i].client.setNoDelay(true);
                clients[i].needs_prompt = true;
                negotiate(clients[i]);
                return;
            }
        }
        // No free slots — reject
        incoming.stop();
    }
}

void TelnetServer::negotiate(TelnetClientSlot &slot) {
    // IAC WILL ECHO — server will echo input
    // IAC WILL SGA  — suppress go-ahead (character-at-a-time mode)
    const uint8_t neg[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA };
    slot.client.write(neg, sizeof(neg));
}

size_t TelnetServer::write(uint8_t b) {
    if (b == '\n' && !prev_was_cr) {
        for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
            if (clients[i].client && clients[i].client.connected())
                clients[i].client.write('\r');
    }
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && clients[i].client.connected())
            clients[i].client.write(b);
    }
    prev_was_cr = (b == '\r');
    return 1;
}

size_t TelnetServer::write(const uint8_t *buffer, size_t size) {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (!clients[i].client || !clients[i].client.connected())
            continue;
        // Write in chunks, expanding bare \n to \r\n
        size_t start = 0;
        bool cr = prev_was_cr;
        for (size_t j = 0; j < size; j++) {
            if (buffer[j] == '\n' && !cr) {
                if (j > start)
                    clients[i].client.write(buffer + start, j - start);
                clients[i].client.write((const uint8_t *)"\r\n", 2);
                start = j + 1;
            }
            cr = (buffer[j] == '\r');
        }
        if (start < size)
            clients[i].client.write(buffer + start, size - start);
    }
    prev_was_cr = (size > 0 && buffer[size - 1] == '\r');
    return size;
}

int TelnetServer::available() {
    checkClient();
    int total = 0;
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && clients[i].client.connected())
            total += clients[i].client.available();
    }
    return total;
}

int TelnetServer::read() {
    checkClient();

    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        TelnetClientSlot &slot = clients[i];
        if (!slot.client || !slot.client.connected())
            continue;

        // Loop to consume IAC sequences from this slot
        while (slot.client.available()) {
            uint8_t b = slot.client.read();

            switch (slot.iac_state) {
            case 0: // normal
                if (b == IAC) {
                    slot.iac_state = 1;
                    break; // consume, continue inner loop
                }
                if (b == 0x04) {
                    // Ctrl+D — disconnect this telnet session
                    slot.client.write((const uint8_t *)"\r\n\033[0m", 5);
                    slot.client.stop();
                    slot.iac_state = 0;
                    slot.iac_cmd = 0;
                    break; // consume, try next slot
                }
                return b;

            case 1: // got IAC
                if (b == IAC) {
                    slot.iac_state = 0;
                    return 0xFF; // IAC IAC = literal 0xFF
                }
                if (b == SB) {
                    slot.iac_state = 3; // subnegotiation
                    break;
                }
                // WILL/WONT/DO/DONT + option byte follows
                slot.iac_cmd = b;
                slot.iac_state = 2;
                break;

            case 2: // got IAC + cmd, this byte is the option
                if (slot.iac_cmd == DO) {
                    // Client confirms our WILL — good, ignore
                } else if (slot.iac_cmd == DONT) {
                    // Client refused — nothing we can do, ignore
                } else if (slot.iac_cmd == WILL) {
                    // Client offers something — accept SGA, refuse others
                    if (b == OPT_SGA) {
                        const uint8_t resp[] = { IAC, DO, OPT_SGA };
                        slot.client.write(resp, sizeof(resp));
                    } else {
                        const uint8_t resp[] = { IAC, DONT, b };
                        slot.client.write(resp, sizeof(resp));
                    }
                } else if (slot.iac_cmd == WONT) {
                    // Client won't do something — fine, ignore
                }
                slot.iac_state = 0;
                break;

            case 3: // subnegotiation — consume until IAC SE
                if (b == IAC) {
                    if (slot.client.available()) {
                        uint8_t se = slot.client.read();
                        if (se == SE) {
                            slot.iac_state = 0;
                        }
                    }
                }
                break;
            }
        }
    }
    return -1;
}

int TelnetServer::peek() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && clients[i].client.connected()
            && clients[i].client.available())
            return clients[i].client.peek();
    }
    return -1;
}

void TelnetServer::flush() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && clients[i].client.connected())
            clients[i].client.flush();
    }
}

bool TelnetServer::connected() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].client && clients[i].client.connected())
            return true;
    }
    return false;
}

bool TelnetServer::hasNewClients() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].needs_prompt)
            return true;
    }
    return false;
}

size_t TelnetServer::sendToNew(const uint8_t *buffer, size_t size) {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (!clients[i].needs_prompt)
            continue;
        if (!clients[i].client || !clients[i].client.connected()) {
            clients[i].needs_prompt = false;
            continue;
        }
        // Write with \r\n translation (same logic as bulk write)
        size_t start = 0;
        bool cr = false;
        for (size_t j = 0; j < size; j++) {
            if (buffer[j] == '\n' && !cr) {
                if (j > start)
                    clients[i].client.write(buffer + start, j - start);
                clients[i].client.write((const uint8_t *)"\r\n", 2);
                start = j + 1;
            }
            cr = (buffer[j] == '\r');
        }
        if (start < size)
            clients[i].client.write(buffer + start, size - start);
    }
    return size;
}

void TelnetServer::clearNewClients() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
        clients[i].needs_prompt = false;
}
