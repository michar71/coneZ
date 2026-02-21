// ConezStream — lightweight replacement for Arduino's Stream/Print classes.
// All output goes through virtual write(); print/printf helpers are non-virtual.

#ifndef CONEZ_STREAM_H
#define CONEZ_STREAM_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

class ConezStream {
public:
    // --- Pure virtual: subclasses must implement ---
    virtual size_t write(uint8_t b) = 0;
    virtual int    available() = 0;
    virtual int    read() = 0;
    virtual int    peek() = 0;

    // --- Virtual with defaults ---
    virtual size_t write(const uint8_t *buf, size_t len);
    virtual void   flush() {}
    virtual ~ConezStream() {}

    // --- Print helpers (all route through write) ---
    size_t print(const char *s);
    size_t print(char c);
    size_t print(int n);
    size_t print(unsigned int n);
    size_t print(long n);
    size_t print(unsigned long n);
    size_t println();
    size_t println(const char *s);
    size_t println(int n);
    size_t printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

    // --- Stream reading helpers ---
    // Reads bytes until terminator found, length reached, or no data available.
    // Returns number of bytes placed in buffer (excluding terminator).
    size_t readBytesUntil(char terminator, char *buffer, size_t length);
};

#endif
