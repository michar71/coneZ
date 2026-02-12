#ifndef DUALSTREAM_H
#define DUALSTREAM_H

#include <Arduino.h>
#include "telnet.h"

// Multiplexes Serial + TelnetServer into a single Stream.
// Writes go to both (Serial skipped if no USB host connected).
// Reads drain Serial first, then Telnet.

class DualStream : public Stream {
public:
    size_t write(uint8_t b) override {
        if (Serial) Serial.write(b);
        telnet.write(b);
        return 1;
    }
    int available() override {
        return Serial.available() + telnet.available();
    }
    int read() override {
        if (Serial.available()) return Serial.read();
        if (telnet.available()) return telnet.read();
        return -1;
    }
    int peek() override {
        if (Serial.available()) return Serial.peek();
        if (telnet.available()) return telnet.peek();
        return -1;
    }
    void flush() override {
        if (Serial) Serial.flush();
        telnet.flush();
    }
};

#endif
