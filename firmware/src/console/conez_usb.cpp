#include "conez_usb.h"
#include "driver/usb_serial_jtag.h"
#include "soc/usb_serial_jtag_struct.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

#define USB_TX_BUF_SIZE  4096
#define USB_RX_BUF_SIZE  256
#define USB_WRITE_TIMEOUT_TICKS  (10 / portTICK_PERIOD_MS)   // 10 ms — fast fail when disconnected

// Peek buffer: the driver has no peek/available API, so we do a 1-byte
// read and cache it here.
static int  peek_byte = -1;

void usb_init(void)
{
    usb_serial_jtag_driver_config_t cfg = {};
    cfg.tx_buffer_size = USB_TX_BUF_SIZE;
    cfg.rx_buffer_size = USB_RX_BUF_SIZE;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    (void)err;  // logged via LED; can't print yet
}

bool usb_connected(void)
{
    // USB host sends SOF (Start-of-Frame) every 1 ms.
    // If the 11-bit frame counter increments over 2 ms, a host is attached.
    uint32_t c1 = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    esp_rom_delay_us(2000);
    uint32_t c2 = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    return c1 != c2;
}

// Cheap, no-delay link check. The host emits an SOF every 1 ms, so the frame
// counter advances constantly while attached and freezes when detached. We
// sample it (no busy-wait, unlike usb_connected()) each write and treat "SOF
// changed within the last ~100 ms" as up. When it's down we write non-blocking
// so a full TX ring can't stall the caller for the 10 ms timeout -- callers hold
// print_mutex, so that stall otherwise throttles every task's console output.
static uint32_t s_last_sof = 0;
static uint32_t s_last_sof_change_ms = 0;
static bool     s_link_up = false;

static bool usb_link_up(void)
{
    uint32_t sof = USB_SERIAL_JTAG.fram_num.sof_frame_index;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (sof != s_last_sof) {
        s_last_sof = sof;
        s_last_sof_change_ms = now;
        s_link_up = true;
    } else if ((uint32_t)(now - s_last_sof_change_ms) > 100) {
        s_link_up = false;   // no SOF for 100 ms -> no host attached
    }
    return s_link_up;
}

size_t usb_write(const uint8_t *buf, size_t len)
{
    if (len == 0) return 0;
    TickType_t timeout = usb_link_up() ? USB_WRITE_TIMEOUT_TICKS : 0;
    int n = usb_serial_jtag_write_bytes(buf, len, timeout);
    return (n > 0) ? (size_t)n : 0;
}

size_t usb_write_byte(uint8_t b)
{
    return usb_write(&b, 1);
}

int usb_read(void)
{
    // Return peek buffer first
    if (peek_byte >= 0) {
        int b = peek_byte;
        peek_byte = -1;
        return b;
    }
    uint8_t b;
    int n = usb_serial_jtag_read_bytes(&b, 1, 0);   // non-blocking
    return (n == 1) ? b : -1;
}

int usb_available(void)
{
    if (peek_byte >= 0) return 1;
    uint8_t b;
    int n = usb_serial_jtag_read_bytes(&b, 1, 0);   // non-blocking peek
    if (n == 1) {
        peek_byte = b;
        return 1;
    }
    return 0;
}

int usb_peek(void)
{
    if (peek_byte >= 0) return peek_byte;
    uint8_t b;
    int n = usb_serial_jtag_read_bytes(&b, 1, 0);
    if (n == 1) {
        peek_byte = b;
        return b;
    }
    return -1;
}

void usb_printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) {
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        usb_write((const uint8_t *)buf, len);
    }
}
