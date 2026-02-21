#ifndef DUALSTREAM_H
#define DUALSTREAM_H

#include "conez_stream.h"
#include "conez_usb.h"
#include "telnet.h"

// Multiplexes USB Serial/JTAG + TelnetServer into a single ConezStream.
// Writes go to both USB and Telnet.
// Reads drain USB first, then Telnet.

class DualStream : public ConezStream {
public:
    size_t write(uint8_t b) override {
        usb_write_byte(b);
        telnet.write(b);
        return 1;
    }
    size_t write(const uint8_t *buffer, size_t size) override {
        usb_write(buffer, size);
        telnet.write(buffer, size);
        return size;
    }
    int available() override {
        return usb_available() + telnet.available();
    }
    int read() override {
        if (usb_available()) return usb_read();
        if (telnet.available()) return telnet.read();
        return -1;
    }
    int peek() override {
        int b = usb_peek();
        if (b >= 0) return b;
        if (telnet.available()) return telnet.peek();
        return -1;
    }
    void flush() override {
        telnet.flush();
    }
};

#endif
