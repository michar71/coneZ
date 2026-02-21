#include "printManager.h"
#include "shell.h"
#include "mqtt_client.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

Stream* OutputStream = &Serial;
SemaphoreHandle_t print_mutex;
uint32_t debug = 0;
bool ts = false;
volatile bool interactive_mode = false;
static bool ansi_enabled = true;   // runtime ANSI toggle (default on)

volatile long threadLoopCount[4] = {0, 0, 0, 0}; // For debugging thread loops

void printManagerInit(Stream* defaultStream)
{
    OutputStream = defaultStream;
    print_mutex = xSemaphoreCreateMutex();
}



void print_ts(void)
{
    // Print the timestamp if enabled (decimal seconds)
    if (ts) {
        unsigned long ms = millis();
        char ts_buf[16];
        snprintf(ts_buf, sizeof(ts_buf), "%lu.%03lu", ms / 1000, ms % 1000);
        if (ansi_enabled) {
            OutputStream->print("\033[36m[");     // cyan bracket
            OutputStream->print("\033[34m");      // blue number
            OutputStream->print(ts_buf);
            OutputStream->print("\033[36m] ");    // cyan bracket
        } else {
            OutputStream->print("[");
            OutputStream->print(ts_buf);
            OutputStream->print("] ");
        }
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

    // Suppress all output while a fullscreen interactive app is active
    if (interactive_mode) return;

    //get Mutex
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE)
    {
        return; // Failed to acquire mutex
    }

    // Early out if source is disabled (COMMANDS always prints — it's CLI output)
    if (source != SOURCE_NONE && source != SOURCE_COMMANDS && !(debug & source)) {
        xSemaphoreGive(print_mutex);
        return;
    }

    // Erase the in-progress command line before printing
    shell.suspendLine(OutputStream);

    // Print source tag (SOURCE_NONE prints with no prefix)
    const char *tag = NULL;
    {
        switch (source) {
            case SOURCE_BASIC:     tag = "BASIC";    break;
            case SOURCE_WASM:      tag = "WASM";     break;
            case SOURCE_SHELL:     tag = "SHELL";    break;
            case SOURCE_COMMANDS:  break;  // no prefix for CLI output
            case SOURCE_SYSTEM:    tag = "SYSTEM";    break;
            case SOURCE_GPS:       tag = "GPS";       break;
            case SOURCE_GPS_RAW:   tag = "GPS_RAW";   break;
            case SOURCE_LORA:      tag = "LORA";      break;
            case SOURCE_LORA_RAW:  tag = "LORA_RAW";  break;
            case SOURCE_OTHER:     tag = "OTHER";     break;
            case SOURCE_WIFI:      tag = "WIFI";      break;
            case SOURCE_FSYNC:     tag = "FSYNC";     break;
            case SOURCE_SENSORS:   tag = "SENSORS";   break;
            case SOURCE_MQTT:      tag = "MQTT";      break;
            case SOURCE_NONE:      break;
        }
        if (tag) {
            print_ts();
            if (ansi_enabled) {
                OutputStream->print("\033[36m[");   // dark cyan bracket
                OutputStream->print("\033[32m");    // green tag
                OutputStream->print(tag);
                OutputStream->print("\033[36m]");   // dark cyan bracket
                OutputStream->print("\033[0m ");    // reset + space
            } else {
                OutputStream->print("[");
                OutputStream->print(tag);
                OutputStream->print("] ");
            }
        }
    }
    vsnprintf(buf, max_txt, format, args);
    OutputStream->print(buf);

    // Publish debug messages to MQTT for remote monitoring
    // Skip: SOURCE_NONE (raw output) and SOURCE_MQTT (prevents infinite loop)
    // SOURCE_COMMANDS forwarded only if its debug flag is enabled
    const char *mqtt_tag = tag ? tag : (source == SOURCE_COMMANDS ? "CMD" : NULL);
    if (mqtt_tag && source != SOURCE_MQTT && mqtt_connected()
        && (source != SOURCE_COMMANDS || (debug & SOURCE_COMMANDS))) {
        static bool in_mqtt_debug = false;
        if (!in_mqtt_debug) {
            in_mqtt_debug = true;
            char topic[48];
            snprintf(topic, sizeof(topic), "conez/%d/debug", config.cone_id);
            char payload[max_txt + 32];
            unsigned long ms = millis();
            snprintf(payload, sizeof(payload), "[%lu.%03lu] [%s] %s", ms / 1000, ms % 1000, mqtt_tag, buf);
            // Strip any ANSI escape sequences from payload
            char *r = payload, *w = payload;
            while (*r) {
                if (*r == '\033' && *(r+1) == '[') {
                    r += 2;
                    while (*r && !(*r >= 'A' && *r <= 'Z') && !(*r >= 'a' && *r <= 'z'))
                        r++;
                    if (*r) r++; // skip terminating letter
                } else {
                    *w++ = *r++;
                }
            }
            *w = '\0';
            mqtt_publish(topic, payload);
            in_mqtt_debug = false;
        }
    }

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


void setInteractive(bool active)
{
    interactive_mode = active;
}

bool isInteractive(void)
{
    return interactive_mode;
}

void setAnsiEnabled(bool enabled)
{
    ansi_enabled = enabled;
}

bool getAnsiEnabled(void)
{
    return ansi_enabled;
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
