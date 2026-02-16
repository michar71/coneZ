/*
 * main.c — sewerpipe: bare-bones MQTT 3.1.1 broker
 *
 * Single-threaded poll() event loop. QoS 0 + QoS 1, retained messages,
 * topic wildcards (+ and #). POSIX only.
 */
#include "sewerpipe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>

#ifndef BUILD_NUMBER
#define BUILD_NUMBER 0
#endif

static broker_t broker;

static void signal_handler(int sig)
{
    (void)sig;
    broker.running = false;
}

static void usage(const char *prog)
{
    printf("sewerpipe — bare-bones MQTT 3.1.1 broker (build %d)\n\n", BUILD_NUMBER);
    printf("Usage: %s [-p port] [-d] [-v] [-h]\n\n", prog);
    printf("  -p port    Listen port (default: %d)\n", DEFAULT_PORT);
    printf("  -d         Daemon mode (fork to background)\n");
    printf("  -v         Verbose logging\n");
    printf("  -h         Show help\n");
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT;
    bool verbose = false;
    bool daemonize = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "sewerpipe: invalid port\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            daemonize = true;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "sewerpipe: unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    /* Bind before forking so errors are visible on the terminal */
    broker_init(&broker, port);
    broker.verbose = verbose;

    if (daemonize) {
        fflush(stdout);
        fflush(stderr);
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }
        if (pid > 0) {
            printf("sewerpipe: daemon started (pid %d)\n", pid);
            return 0;
        }
        /* Child: new session, detach from terminal */
        setsid();
        (void)!freopen("/dev/null", "r", stdin);
        (void)!freopen("/dev/null", "w", stdout);
        (void)!freopen("/dev/null", "w", stderr);
    }

    struct pollfd fds[MAX_CLIENTS + 1];

    while (broker.running) {
        int nfds = 0;

        /* Listen socket */
        fds[nfds].fd = broker.listen_fd;
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;

        /* Client sockets */
        int client_map[MAX_CLIENTS]; /* maps pollfd index -> client index */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (broker.clients[i].fd >= 0) {
                client_map[nfds - 1] = i;
                fds[nfds].fd = broker.clients[i].fd;
                fds[nfds].events = POLLIN;
                fds[nfds].revents = 0;
                nfds++;
            }
        }

        int ret = poll(fds, nfds, 1000);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("poll");
            break;
        }

        /* Accept new connections */
        if (fds[0].revents & POLLIN)
            broker_accept(&broker);

        /* Process client data */
        for (int i = 1; i < nfds; i++) {
            if (!(fds[i].revents & (POLLIN | POLLERR | POLLHUP)))
                continue;

            int ci = client_map[i - 1];
            client_t *c = &broker.clients[ci];
            if (c->fd < 0) continue;

            if (fds[i].revents & (POLLERR | POLLHUP)) {
                broker_disconnect(&broker, c);
                continue;
            }

            /* Read data */
            ssize_t n = read(c->fd, c->rx_buf + c->rx_len,
                             RX_BUF_SIZE - c->rx_len);
            if (n <= 0) {
                broker_disconnect(&broker, c);
                continue;
            }
            c->rx_len += n;

            /* Parse packets from buffer */
            while (c->fd >= 0 && c->rx_len > 0) {
                uint8_t pkt_type, flags;
                const uint8_t *payload;
                uint32_t payload_len;

                int consumed = mqtt_parse_packet(c->rx_buf, c->rx_len,
                                                 &pkt_type, &flags,
                                                 &payload, &payload_len);
                if (consumed == 0) break;   /* incomplete */
                if (consumed < 0) {
                    broker_disconnect(&broker, c);
                    break;
                }

                broker_handle_packet(&broker, c, pkt_type, flags,
                                     payload, payload_len);

                /* Shift buffer */
                if (c->fd >= 0 && (uint32_t)consumed < c->rx_len) {
                    memmove(c->rx_buf, c->rx_buf + consumed,
                            c->rx_len - consumed);
                }
                c->rx_len -= consumed;
            }
        }

        /* Periodic: keep-alive checks and QoS 1 retries */
        time_t now;
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            now = ts.tv_sec;
        }

        for (int i = 0; i < MAX_CLIENTS; i++) {
            client_t *c = &broker.clients[i];
            if (c->fd < 0) continue;

            /* Keep-alive timeout: 1.5x the keep_alive period */
            if (c->state == CS_CONNECTED && c->keep_alive > 0) {
                time_t deadline = c->last_activity + c->keep_alive +
                                  (c->keep_alive / 2);
                if (now > deadline) {
                    if (broker.verbose)
                        printf("sewerpipe: keep-alive timeout for '%s'\n",
                               c->client_id);
                    broker_disconnect(&broker, c);
                    continue;
                }
            }

            /* CS_NEW timeout: 10 seconds to send CONNECT */
            if (c->state == CS_NEW && now - c->last_activity > 10) {
                if (broker.verbose)
                    printf("sewerpipe: connect timeout (fd %d)\n", c->fd);
                broker_disconnect(&broker, c);
                continue;
            }

            /* QoS 1 retries */
            if (c->state == CS_CONNECTED)
                inflight_retry(&broker, c);
        }
    }

    /* Clean shutdown */
    printf("\nsewerpipe: shutting down\n");
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (broker.clients[i].fd >= 0)
            broker_disconnect(&broker, &broker.clients[i]);
    }
    for (int i = 0; i < MAX_RETAINED; i++) {
        free(broker.retained[i].payload);
    }
    close(broker.listen_fd);

    return 0;
}
