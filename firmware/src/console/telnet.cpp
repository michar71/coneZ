#include "telnet.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include <errno.h>

// Local monotonic ms — avoids pulling main.h (board/led/i2c) into this module.
static inline uint32_t telnet_now_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

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
        clients[i].iac_sb_bytes = 0;
        clients[i].needs_prompt = false;
        clients[i].last_rx_ms = 0;
    }
}

// Max bytes to consume inside an IAC SB..SE block before giving up — a peer
// sending crafted non-terminated subneg data can't lock a client in state 3.
static const uint16_t TELNET_SB_MAX = 256;

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

    if (listen(listen_fd, TELNET_MAX_CLIENTS) < 0) {
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
    slot.iac_sb_bytes = 0;
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
    uint32_t now = telnet_now_ms();

    // Clean up disconnected slots — peek with recv to detect closed connections
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0) {
            // Check if peer closed by attempting a peek.
            //
            // NOTE: this only reports 0 for a peer that closed with an EMPTY
            // receive buffer. A peer that sent a command and then closed leaves
            // that data queued, so the peek returns >0 and looks identical to a
            // live session. Such a slot is only reclaimed once read() drains the
            // data and sees the subsequent 0 — and if nothing is draining input,
            // it is never reclaimed at all. The idle timeout below is what
            // guarantees the slot comes back either way.
            uint8_t tmp;
            int ret = recv(clients[i].fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
            if (ret == 0) {
                // Peer closed
                slot_close(clients[i]);
                continue;
            } else if (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                slot_close(clients[i]);
                continue;
            }

            // Idle reap — telnet_now_ms() wraps at ~49 days; unsigned subtraction
            // stays correct across the wrap.
            if ((uint32_t)(now - clients[i].last_rx_ms) > TELNET_IDLE_TIMEOUT_MS)
                slot_close(clients[i]);
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
            clients[i].iac_sb_bytes = 0;
            clients[i].needs_prompt = true;
            clients[i].last_rx_ms = now;   // fresh session — start the idle clock

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

// How many bytes are waiting on this socket, without using FIONREAD.
//
// ioctl(FIONREAD) DOES NOT WORK on lwIP sockets in this build: lwIP compiles the
// FIONREAD case out unless LWIP_SO_RCVBUF or LWIP_FIONREAD_LINUXMODE is enabled, and
// CONFIG_LWIP_SO_RCVBUF defaults to n (its Kconfig help: "allows checking for
// available data on a netconn"). The call therefore fails every time, available()
// returned 0, DualStream::read() never took the telnet branch, and typed input was
// silently discarded while output kept working -- output uses send(), which doesn't
// touch ioctl. That was the telnet-input regression.
//
// A non-blocking MSG_PEEK needs no lwIP options and the sockets are already
// O_NONBLOCK. Peeking a small window is enough: callers only need "is there input",
// and read() consumes one byte at a time anyway.
static int slot_pending(int fd) {
    uint8_t probe[64];
    int n = recv(fd, probe, sizeof(probe), MSG_PEEK | MSG_DONTWAIT);
    return (n > 0) ? n : 0;
}

int TelnetServer::available() {
    checkClient();
    int total = 0;
    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        if (clients[i].fd >= 0)
            total += slot_pending(clients[i].fd);
    }
    return total;
}

int TelnetServer::read() {
    checkClient();

    for (int i = 0; i < TELNET_MAX_CLIENTS; i++) {
        TelnetClientSlot &slot = clients[i];
        if (slot.fd < 0)
            continue;

        // Loop to consume IAC sequences from this slot.
        // The socket is O_NONBLOCK, so recv() itself reports "nothing pending" via
        // EAGAIN -- no FIONREAD probe needed (and none available; see slot_pending).
        for (;;) {
            uint8_t b;
            int ret = recv(slot.fd, &b, 1, MSG_DONTWAIT);
            if (ret == 0) {
                slot_close(slot);   // peer closed
                break;
            }
            if (ret < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK)
                    slot_close(slot);
                break;              // nothing to read right now
            }
            slot.last_rx_ms = telnet_now_ms();   // peer is alive — defer the idle reap

            switch (slot.iac_state) {
            case 0: // normal
                if (b == IAC) {
                    slot.iac_state = 1;
                    break; // consume, continue inner loop
                }
                if (b == 0x04) {
                    // Ctrl+D — disconnect this telnet session. The farewell is
                    // 6 bytes (\r \n ESC [ 0 m); sending 5 dropped the final 'm'
                    // and left a dangling ESC[0 on the client.
                    slot_send(slot, (const uint8_t *)"\r\n\033[0m", 6);
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
                    slot.iac_sb_bytes = 0;
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
                    // Don't peek the next byte with a nested recv: if the SE is
                    // in the next TCP segment that recv returns EAGAIN, the IAC
                    // is lost and the SE is then swallowed as data, pinning us in
                    // state 3 until TELNET_SB_MAX and eating real input. Track it
                    // as its own state instead so IAC/SE can span recv calls.
                    slot.iac_state = 4;
                    break;
                }
                // Guard against a malformed subneg that never ends — a peer
                // could otherwise pin us in this state indefinitely.
                if (++slot.iac_sb_bytes > TELNET_SB_MAX) {
                    slot.iac_state = 0;
                    slot.iac_sb_bytes = 0;
                }
                break;

            case 4: // subnegotiation, saw IAC — this byte decides
                if (b == SE) {
                    slot.iac_state = 0;      // IAC SE ends the subnegotiation
                    slot.iac_sb_bytes = 0;
                } else {
                    // IAC IAC (literal 0xFF) or IAC <other> inside SB: stay in
                    // the subnegotiation, still bounded by TELNET_SB_MAX.
                    slot.iac_state = 3;
                    if (++slot.iac_sb_bytes > TELNET_SB_MAX) {
                        slot.iac_state = 0;
                        slot.iac_sb_bytes = 0;
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
            uint8_t b;
            if (recv(clients[i].fd, &b, 1, MSG_PEEK | MSG_DONTWAIT) == 1)
                return b;
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
