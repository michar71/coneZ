#include "conez_stream.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

size_t ConezStream::write(const uint8_t *buf, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++)
        n += write(buf[i]);
    return n;
}

size_t ConezStream::print(const char *s)
{
    if (!s) return 0;
    size_t len = strlen(s);
    return write((const uint8_t *)s, len);
}

size_t ConezStream::print(char c)
{
    return write((uint8_t)c);
}

size_t ConezStream::print(int n)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    return print(buf);
}

size_t ConezStream::print(unsigned int n)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", n);
    return print(buf);
}

size_t ConezStream::print(long n)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld", n);
    return print(buf);
}

size_t ConezStream::print(unsigned long n)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", n);
    return print(buf);
}

size_t ConezStream::println()
{
    return print("\r\n");
}

size_t ConezStream::println(const char *s)
{
    size_t n = print(s);
    n += println();
    return n;
}

size_t ConezStream::println(int n)
{
    size_t cnt = print(n);
    cnt += println();
    return cnt;
}

size_t ConezStream::printf(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return 0;
    if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
    return write((const uint8_t *)buf, (size_t)len);
}

size_t ConezStream::readBytesUntil(char terminator, char *buffer, size_t length)
{
    size_t index = 0;
    while (index < length) {
        // Wait for data with a short yield
        while (!available()) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        int c = read();
        if (c < 0) break;
        if ((char)c == terminator) break;
        buffer[index++] = (char)c;
    }
    return index;
}
