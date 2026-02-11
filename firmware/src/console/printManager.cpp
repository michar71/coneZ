#include "printManager.h"
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
    // Print the source prefix if needed
    if (ts) {
        unsigned long currentMillis = millis();
        OutputStream->print("[");
        OutputStream->print(currentMillis);
        OutputStream->print("] ");
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

    //va_list args;
    //va_start(args, format);

    //get Mutex
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE)
    {
        return; // Failed to acquire mutex
    }

    switch (source) {
        case SOURCE_BASIC:
            if (debug & SOURCE_BASIC)
            {
                print_ts();
                OutputStream->print("[BASIC] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_WASM:
            if (debug & SOURCE_WASM)
            {
                print_ts();
                OutputStream->print("[WASM] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_SHELL:
            if (debug & SOURCE_SHELL)
            {
                print_ts();
                OutputStream->print("[SHELL] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);    
            }
            break;

        case SOURCE_COMMANDS:
            if (debug & SOURCE_COMMANDS)
            {
                print_ts();
                OutputStream->print("[COMMANDS] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_SYSTEM:
            if (debug & SOURCE_SYSTEM)
            {
                print_ts();
                OutputStream->print("[SYSTEM] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_GPS:
            if (debug & SOURCE_GPS)
            {
                print_ts();
                OutputStream->print("[GPS] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_GPS_RAW:
            if (debug & SOURCE_GPS_RAW)
            {
                print_ts();
                OutputStream->print("[GPS_RAW] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_LORA:
            if (debug & SOURCE_LORA)
            {
                print_ts();
                OutputStream->print("[LORA] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);   
            }
            break;

        case SOURCE_LORA_RAW:
            if (debug & SOURCE_LORA_RAW)
            {
                print_ts();
                OutputStream->print("[LORA_RAW] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);   
            }
            break;

        case SOURCE_OTHER:
            if (debug & SOURCE_OTHER)
            {
                print_ts();
                OutputStream->print("[OTHER] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_WIFI:
            if (debug & SOURCE_WIFI)
            {
                print_ts();
                OutputStream->print("[WIFI] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_FSYNC:
            if (debug & SOURCE_FSYNC)
            {
                print_ts();
                OutputStream->print("[FSYNC] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_SENSORS:
            if (debug & SOURCE_SENSORS)
            {
                print_ts();
                OutputStream->print("[SENSORS] ");
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            }
            break;

        case SOURCE_NONE:
                vsnprintf(buf,max_txt, format, args);
                OutputStream->print(buf);
            break;            
    }
    
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
