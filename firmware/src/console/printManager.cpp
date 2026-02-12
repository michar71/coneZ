#include "printManager.h"
#include "shell.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

Stream* OutputStream = &Serial;
SemaphoreHandle_t print_mutex;
uint32_t debug = 0;
bool ts = false;

volatile long threadLoopCount[4] = {0, 0, 0, 0}; // For debugging thread loops

void printManagerInit(Stream* defaultStream)
{
    OutputStream = defaultStream;
    print_mutex = xSemaphoreCreateMutex();
}



void print_ts(void)
{
    // Print the timestamp if enabled
    if (ts) {
        unsigned long currentMillis = millis();
#ifdef SHELL_USE_ANSI
        OutputStream->print("\033[36m[");     // cyan bracket
        OutputStream->print("\033[34m");      // blue number
        OutputStream->print(currentMillis);
        OutputStream->print("\033[36m] ");    // cyan bracket
#else
        OutputStream->print("[");
        OutputStream->print(currentMillis);
        OutputStream->print("] ");
#endif
    }
}


// va_list version of printfnl
void vprintfnl( source_e source, const char *format, va_list args )
{
    const int max_txt = 255;
    char buf[max_txt];
    if (OutputStream == NULL)
    {
        return; // No output stream set
    }

    //get Mutex
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE)
    {
        return; // Failed to acquire mutex
    }

    // Early out if source is disabled
    if (source != SOURCE_NONE && !(debug & source)) {
        xSemaphoreGive(print_mutex);
        return;
    }

    // Erase the in-progress command line before printing
    shell.suspendLine(OutputStream);

    // Print source tag (SOURCE_NONE prints with no prefix)
    {
        const char *tag = NULL;
        switch (source) {
            case SOURCE_BASIC:     tag = "BASIC";    break;
            case SOURCE_WASM:      tag = "WASM";     break;
            case SOURCE_SHELL:     tag = "SHELL";    break;
            case SOURCE_COMMANDS:  tag = "COMMANDS";  break;
            case SOURCE_SYSTEM:    tag = "SYSTEM";    break;
            case SOURCE_GPS:       tag = "GPS";       break;
            case SOURCE_GPS_RAW:   tag = "GPS_RAW";   break;
            case SOURCE_LORA:      tag = "LORA";      break;
            case SOURCE_LORA_RAW:  tag = "LORA_RAW";  break;
            case SOURCE_OTHER:     tag = "OTHER";     break;
            case SOURCE_WIFI:      tag = "WIFI";      break;
            case SOURCE_FSYNC:     tag = "FSYNC";     break;
            case SOURCE_SENSORS:   tag = "SENSORS";   break;
            case SOURCE_NONE:      break;
        }
        if (tag) {
            print_ts();
#ifdef SHELL_USE_ANSI
            OutputStream->print("\033[36m[");   // dark cyan bracket
            OutputStream->print("\033[32m");    // green tag
            OutputStream->print(tag);
            OutputStream->print("\033[36m]");   // dark cyan bracket
            OutputStream->print("\033[0m ");    // reset + space
#else
            OutputStream->print("[");
            OutputStream->print(tag);
            OutputStream->print("] ");
#endif
        }
    }
    vsnprintf(buf, max_txt, format, args);
    OutputStream->print(buf);

    // Redraw the command line after our output
    shell.resumeLine(OutputStream);

    //return mutex
    xSemaphoreGive(print_mutex);
}


// Overloaded printfnl() that accepts F() wrapped __FlashStringHelper strings.
void printfnl( source_e source, const __FlashStringHelper *format, ... )
{
    const int max_fmt = 128;
    char fmt[max_fmt];

    // Copy format string from PROGMEM (flash) to RAM
    strncpy_P(fmt, (const char *)format, max_fmt);
    fmt[max_fmt - 1] = 0;

    va_list args;
    va_start(args, format);
    vprintfnl(source, fmt, args);
    va_end(args);
}


void printfnl( source_e source, const char *format, ... )
{
    va_list args;
    va_start(args, format);
    vprintfnl(source, format, args);
    va_end(args);
}



/*
void printfl(source_e source, const char *format, ...)
{
    if (OutputStream == NULL) 
    {
        return; // No output stream set
    }

    va_list args;
    va_start(args, format);
    printfnl(source, format, args);
    va_end(args);
    //get Mutex
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE) 
    {
        return; // Failed to acquire mutex
    }

    OutputStream->println(); // Add a newline after the formatted output
    //return mutex
    xSemaphoreGive(print_mutex);
}
*/

void setStream(Stream *stream)
{
    getLock();
    if (stream != NULL) 
        OutputStream = stream;
    releaseLock();    
}

Stream* getStream(void)
{
    return OutputStream;
}


void setDebugLevel(source_e system, bool enable)
{
    if (enable) {
        debug |= system;
    } else {
        debug &= ~system;
    }
}


void setDebugOff( void )
{
    // Turn off everything except SYSTEM and SHELL and COMMAND debug messages.
    debug &= ( SOURCE_SYSTEM | SOURCE_SHELL | SOURCE_COMMANDS );
}


void showTimestamps(bool enable)
{
    ts = enable;
}


void getLock(void)
{
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE) {
        // Handle error if needed
    }
}

void releaseLock(void)
{
    xSemaphoreGive(print_mutex);
}

bool getDebug(source_e source)
{
    return (debug & source) != 0;
}


void inc_thread_count(int thread)
{
    if (thread < 0 || thread >= 4) {
        return; // Invalid thread index
    }
    threadLoopCount[thread]++;
}


long get_thread_count(int thread)
{
    if (thread < 0 || thread >= 4) {
        return 0; // Invalid thread index
    }
    return threadLoopCount[thread];
}
