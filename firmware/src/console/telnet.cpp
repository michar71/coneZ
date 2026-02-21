#include "telnet.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <errno.h>

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

TelnetServer::TelnetServer(uint16_t p)
    : listen_fd(-1), port(p), prev_was_cr(false) {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].iac_state = 0;
        clients[i].iac_cmd = 0;
        clients[i].needs_prompt = false;
    }
}

void TelnetServer::begin() {
    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) return;

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(listen_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    if (listen(listen_fd, 2) < 0) {
        close(listen_fd);
        listen_fd = -1;
        return;
    }

    // Non-blocking accept
    int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
}

bool TelnetServer::slot_connected(TelnetClientSlot &slot) {
    return slot.fd >= 0;
}

void TelnetServer::slot_close(TelnetClientSlot &slot) {
    if (slot.fd >= 0) {
        close(slot.fd);
        slot.fd = -1;
    }
    slot.iac_state = 0;
    slot.iac_cmd = 0;
}

int TelnetServer::slot_send(TelnetClientSlot &slot, const uint8_t *buf, size_t len) {
    if (slot.fd < 0) return -1;
    int ret = send(slot.fd, buf, len, 0);
    if (ret < 0) {
        int err = errno;
        if (err == ECONNRESET || err == EPIPE || err == ENOTCONN) {
            slot_close(slot);
            return -1;
        }
        // EAGAIN/EWOULDBLOCK — just drop this write (non-blocking)
        return 0;
    }
    return ret;
}

void TelnetServer::checkClient() {
    // Clean up disconnected slots — peek with recv to detect closed connections
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            // Check if peer closed by attempting a peek
            uint8_t tmp;
            int ret = recv(clients[i].fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
            if (ret == 0) {
                // Peer closed
                slot_close(clients[i]);
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                slot_close(clients[i]);
            }
        }
    }

    if (listen_fd < 0) return;

    // Accept new connection into first free slot
    int incoming = accept(listen_fd, NULL, NULL);
    if (incoming < 0) return;  // EAGAIN = no pending connection

    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd < 0) {
            clients[i].fd = incoming;
            clients[i].iac_state = 0;
            clients[i].iac_cmd = 0;
            clients[i].needs_prompt = true;

            // Set non-blocking + TCP_NODELAY
            int flags = fcntl(incoming, F_GETFL, 0);
            fcntl(incoming, F_SETFL, flags | O_NONBLOCK);
            int opt = 1;
            setsockopt(incoming, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            negotiate(clients[i]);
            return;
        }
    }
    // No free slots — reject
    close(incoming);
}

void TelnetServer::negotiate(TelnetClientSlot &slot) {
    // IAC WILL ECHO — server will echo input
    // IAC WILL SGA  — suppress go-ahead (character-at-a-time mode)
    const uint8_t neg[] = { IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA };
    slot_send(slot, neg, sizeof(neg));
}

size_t TelnetServer::write(uint8_t b) {
    if (b == '\n' && !prev_was_cr) {
        uint8_t cr = '\r';
        for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
            if (clients[i].fd >= 0)
                slot_send(clients[i], &cr, 1);
    }
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0)
            slot_send(clients[i], &b, 1);
    }
    prev_was_cr = (b == '\r');
    return 1;
}

size_t TelnetServer::write(const uint8_t *buffer, size_t size) {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd < 0)
            continue;
        // Write in chunks, expanding bare \n to \r\n
        size_t start = 0;
        bool cr = prev_was_cr;
        for (size_t j = 0; j < size; j++) {
            if (buffer[j] == '\n' && !cr) {
                if (j > start)
                    slot_send(clients[i], buffer + start, j - start);
                slot_send(clients[i], (const uint8_t *)"\r\n", 2);
                start = j + 1;
            }
            cr = (buffer[j] == '\r');
        }
        if (start < size)
            slot_send(clients[i], buffer + start, size - start);
    }
    prev_was_cr = (size > 0 && buffer[size - 1] == '\r');
    return size;
}

int TelnetServer::available() {
    checkClient();
    int total = 0;
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            int count = 0;
            if (ioctl(clients[i].fd, FIONREAD, &count) == 0)
                total += count;
        }
    }
    return total;
}

int TelnetServer::read() {
    checkClient();

    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        TelnetClientSlot &slot = clients[i];
        if (slot.fd < 0)
            continue;

        // Loop to consume IAC sequences from this slot
        for (;;) {
            int count = 0;
            if (ioctl(slot.fd, FIONREAD, &count) < 0 || count <= 0)
                break;

            uint8_t b;
            int ret = recv(slot.fd, &b, 1, 0);
            if (ret <= 0) {
                if (ret == 0) slot_close(slot);  // peer closed
                break;
            }

            switch (slot.iac_state) {
            case 0: // normal
                if (b == IAC) {
                    slot.iac_state = 1;
                    break; // consume, continue inner loop
                }
                if (b == 0x04) {
                    // Ctrl+D — disconnect this telnet session
                    slot_send(slot, (const uint8_t *)"\r\n\033[0m", 5);
                    slot_close(slot);
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
                        slot_send(slot, resp, sizeof(resp));
                    } else {
                        const uint8_t resp[] = { IAC, DONT, b };
                        slot_send(slot, resp, sizeof(resp));
                    }
                } else if (slot.iac_cmd == WONT) {
                    // Client won't do something — fine, ignore
                }
                slot.iac_state = 0;
                break;

            case 3: // subnegotiation — consume until IAC SE
                if (b == IAC) {
                    int cnt = 0;
                    if (ioctl(slot.fd, FIONREAD, &cnt) == 0 && cnt > 0) {
                        uint8_t se;
                        if (recv(slot.fd, &se, 1, 0) == 1 && se == SE) {
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
        if (clients[i].fd >= 0) {
            int count = 0;
            if (ioctl(clients[i].fd, FIONREAD, &count) == 0 && count > 0) {
                uint8_t b;
                if (recv(clients[i].fd, &b, 1, MSG_PEEK) == 1)
                    return b;
            }
        }
    }
    return -1;
}

void TelnetServer::flush() {
    // No-op — TCP_NODELAY is set on all client sockets
}

bool TelnetServer::connected() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0)
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
        if (clients[i].fd < 0) {
            clients[i].needs_prompt = false;
            continue;
        }
        // Write with \r\n translation (same logic as bulk write)
        size_t start = 0;
        bool cr = false;
        for (size_t j = 0; j < size; j++) {
            if (buffer[j] == '\n' && !cr) {
                if (j > start)
                    slot_send(clients[i], buffer + start, j - start);
                slot_send(clients[i], (const uint8_t *)"\r\n", 2);
                start = j + 1;
            }
            cr = (buffer[j] == '\r');
        }
        if (start < size)
            slot_send(clients[i], buffer + start, size - start);
    }
    return size;
}

void TelnetServer::clearNewClients() {
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++)
        clients[i].needs_prompt = false;
}
