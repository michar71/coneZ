#ifndef CONEZ_USB_H
#define CONEZ_USB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Thin wrapper around ESP-IDF usb_serial_jtag driver.
// Replaces Arduino HWCDC (Serial) — all USB access goes through the driver's
// ring buffer and ISR, eliminating the cross-core FIFO race condition.

void    usb_init(void);                              // Install driver (call once from setup)
bool    usb_connected(void);                         // USB host attached (SOF frames arriving)
size_t  usb_write(const uint8_t *buf, size_t len);   // Write with short timeout
size_t  usb_write_byte(uint8_t b);                   // Single byte write
int     usb_read(void);                              // Non-blocking read, -1 if empty
int     usb_available(void);                         // Bytes available (0 or 1 via peek)
int     usb_peek(void);                              // Peek without consuming
void    usb_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
