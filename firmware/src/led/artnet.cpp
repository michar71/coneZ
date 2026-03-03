/*
 * artnet.cpp — ArtNet UDP receiver for ConeZ firmware
 *
 * Listens on UDP port 6454. Each LED channel is mapped to a configurable
 * (universe, DMX address) pair via config.artnet_uni1..4 / artnet_dmx1..4.
 * DMX addresses are 1-indexed (standard convention); 0 means the channel is
 * disabled. Received RGB triplets are written directly into leds1..4 and
 * led_show() is called to trigger the render task.
 *
 * The task waits for WiFi if not yet connected, and reopens the socket if
 * the connection drops and resumes.
 */

#include <string.h>
#include <errno.h>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "artnet.h"
#include "config.h"
#include "led.h"
#include "conez_wifi.h"
#include "printManager.h"
#include "main.h"

#define ARTNET_PORT      6454
#define ARTNET_HEADER    18
#define ARTNET_OPOUTPUT  0x5000u
#define BUF_SIZE         (ARTNET_HEADER + 512)

static TaskHandle_t      s_task = NULL;
static volatile bool     s_running = false;
static volatile uint32_t s_rx_packets = 0;
static volatile uint32_t s_rx_frames  = 0;

// ---------------------------------------------------------------------------
// Apply one received ArtNet universe to any matching LED channels
// ---------------------------------------------------------------------------

static void apply_universe(int universe, const uint8_t *dmx, int dmx_len)
{
    const int unis[4]   = { config.artnet_uni1, config.artnet_uni2,
                             config.artnet_uni3, config.artnet_uni4 };
    const int addrs[4]  = { config.artnet_dmx1, config.artnet_dmx2,
                             config.artnet_dmx3, config.artnet_dmx4 };
    const int counts[4] = { config.led_count1,  config.led_count2,
                             config.led_count3,  config.led_count4 };
    CRGB *ptrs[4]       = { leds1, leds2, leds3, leds4 };

    bool dirty = false;

    for (int ch = 0; ch < 4; ch++) {
        if (addrs[ch] == 0)        continue;   // 0 = channel disabled
        if (unis[ch] != universe)  continue;   // wrong universe
        if (!ptrs[ch])             continue;   // buffer not allocated

        // DMX address is 1-indexed; convert to 0-indexed byte offset
        int base  = addrs[ch] - 1;
        int count = counts[ch];

        for (int i = 0; i < count; i++) {
            int off = base + i * 3;
            if (off + 2 >= dmx_len) break;  // incomplete triplet — stop
            ptrs[ch][i].r = dmx[off];
            ptrs[ch][i].g = dmx[off + 1];
            ptrs[ch][i].b = dmx[off + 2];
        }
        dirty = true;
    }

    if (dirty) {
        s_rx_frames++;
        led_show();
    }
}

// ---------------------------------------------------------------------------
// Receiver task
// ---------------------------------------------------------------------------

static void artnet_task_fun(void *arg)
{
    (void)arg;

    uint8_t *buf = (uint8_t *)malloc(BUF_SIZE);
    if (!buf) {
        printfnl(SOURCE_SYSTEM, "[ArtNet] Out of memory\n");
        s_task = NULL;
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    int sock = -1;

    while (s_running) {
        // Wait for WiFi; close socket if it was open before the disconnect
        if (!wifi_is_connected()) {
            if (sock >= 0) {
                close(sock);
                sock = -1;
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // (Re)open and bind the socket
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (sock < 0) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            int yes = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);
            addr.sin_port        = htons(ARTNET_PORT);

            if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
                printfnl(SOURCE_SYSTEM, "[ArtNet] bind failed\n");
                close(sock);
                sock = -1;
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            // 200 ms receive timeout so the loop can check s_running
            struct timeval tv;
            tv.tv_sec  = 0;
            tv.tv_usec = 200000;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            printfnl(SOURCE_SYSTEM, "[ArtNet] Listening on UDP port %d\n", ARTNET_PORT);
        }

        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int n = recvfrom(sock, buf, BUF_SIZE, 0,
                         (struct sockaddr *)&from, &fromlen);

        if (!s_running) break;

        if (n < 0) {
            // EAGAIN/EWOULDBLOCK = normal timeout; anything else = socket error
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close(sock);
                sock = -1;
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            continue;
        }

        // Validate Art-Net header
        if (n < ARTNET_HEADER)                       continue;
        if (memcmp(buf, "Art-Net", 8) != 0)          continue;
        uint16_t opcode = (uint16_t)(buf[8] | ((unsigned)buf[9] << 8));
        if (opcode != ARTNET_OPOUTPUT)               continue;

        int universe = (int)(buf[14] | ((buf[15] & 0x7F) << 8));
        int dmx_len  = (int)((buf[16] << 8) | buf[17]);
        if (dmx_len < 2 || dmx_len > 512)            continue;
        if (n < ARTNET_HEADER + dmx_len)             continue;

        s_rx_packets++;
        apply_universe(universe, buf + ARTNET_HEADER, dmx_len);
    }

    free(buf);
    if (sock >= 0) close(sock);
    s_task    = NULL;
    s_running = false;
    vTaskDelete(NULL);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void artnet_setup(void)
{
    if (config.artnet_enabled)
        artnet_start();
}

void artnet_start(void)
{
    if (s_task) return;     // already running
    s_running    = true;
    s_rx_packets = 0;
    s_rx_frames  = 0;
    xTaskCreate(artnet_task_fun, "ArtNet", 4096, NULL, 5, &s_task);
    printfnl(SOURCE_SYSTEM, "[ArtNet] Started\n");
}

void artnet_stop(void)
{
    if (!s_task) return;
    s_running = false;
    // Task exits within ~200 ms on the next recvfrom timeout
    printfnl(SOURCE_SYSTEM, "[ArtNet] Stopping\n");
}

bool     artnet_running(void)    { return s_task != NULL; }
uint32_t artnet_rx_packets(void) { return s_rx_packets; }
uint32_t artnet_rx_frames(void)  { return s_rx_frames; }
