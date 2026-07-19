#include "printManager.h"
#include "shell.h"
#include "conez_mqtt.h"
#include "config.h"
#include "psram.h"
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

ConezStream* OutputStream = NULL;
SemaphoreHandle_t print_mutex;
uint32_t debug = 0;
bool ts = false;
volatile bool interactive_mode = false;
static bool ansi_enabled = true;   // runtime ANSI toggle (default on)

volatile long threadLoopCount[4] = {0, 0, 0, 0}; // For debugging thread loops

// ---- Debug log: PSRAM ring buffer + file sink ----
#define LOG_ENTRY_SIZE  256     // bytes per slot (page-aligned for PSRAM cache)
static uint32_t log_ring_base = 0;   // PSRAM address (0 = not allocated)
static int      log_ring_slots = 0;  // set at runtime by log_init()
static int      log_ring_head = 0;   // next write slot
static int      log_ring_count = 0;  // entries written (clamped to log_ring_slots)
static FILE    *log_file = NULL;      // file sink (open when non-NULL)

void printManagerInit(ConezStream* defaultStream)
{
    OutputStream = defaultStream;
    print_mutex = xSemaphoreCreateMutex();
}



void print_ts(void)
{
    // Print the timestamp if enabled (decimal seconds)
    if (ts) {
        unsigned long ms = uptime_ms();
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

    // The MQTT debug sink is published AFTER print_mutex is released — see the
    // note at the publish site below. Staged here so it outlives the critical
    // section (same stack frame, so no other task can clobber it).
    char sink_buf[max_txt + 32];
    char mqtt_topic[48];
    bool mqtt_pending = false;

    //get Mutex
    if (xSemaphoreTake(print_mutex, portMAX_DELAY) != pdTRUE)
    {
        return; // Failed to acquire mutex
    }

    // Early out if source is disabled (COMMANDS always prints — it's CLI output)
    // COMMANDS_PROMPT uses COMMANDS debug flag for gating
    if (source != SOURCE_NONE && source != SOURCE_COMMANDS
        && source != SOURCE_COMMANDS_PROMPT && !(debug & source)) {
        xSemaphoreGive(print_mutex);
        return;
    }

    vsnprintf(buf, max_txt, format, args);

    // SOURCE_COMMANDS_PROMPT skips console output — sinks only (MQTT, file log)
    const char *tag = NULL;
    bool console_output = (source != SOURCE_COMMANDS_PROMPT);

    if (console_output) {
        // Erase the in-progress command line before printing
        shell.suspendLine(OutputStream);

        // Print source tag (SOURCE_NONE prints with no prefix)
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
            case SOURCE_LORA_DIST: tag = "LORA_DIST"; break;
            case SOURCE_OTHER:     tag = "OTHER";     break;
            case SOURCE_WIFI:      tag = "WIFI";      break;
            case SOURCE_SENSORS:   tag = "SENSORS";   break;
            case SOURCE_MQTT:      tag = "MQTT";      break;
            case SOURCE_NONE:      break;
            case SOURCE_COMMANDS_PROMPT: break;
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
        OutputStream->print(buf);
    }

    // ---- Sinks: ring buffer, file, MQTT ----
    // Build sink payload once, strip ANSI once, send to all sinks.
    // Skip SOURCE_NONE (raw output).
    // SOURCE_COMMANDS/PROMPT forwarded only if COMMANDS debug flag is enabled.
    const char *sink_tag = tag;
    if (!sink_tag && (source == SOURCE_COMMANDS || source == SOURCE_COMMANDS_PROMPT))
        sink_tag = "CMD";
    bool commands_gated = (source == SOURCE_COMMANDS || source == SOURCE_COMMANDS_PROMPT)
                          && !(debug & SOURCE_COMMANDS);
    if (sink_tag && !commands_gated) {
        unsigned long ms = uptime_ms();
        snprintf(sink_buf, sizeof(sink_buf), "[%lu.%03lu] [%s] %s", ms / 1000, ms % 1000, sink_tag, buf);
        // Strip ANSI escape sequences in-place. Handles:
        //   ESC [ ... final (0x40-0x7E)    -> CSI (SGR, cursor, etc.)
        //   ESC ] ... BEL | ESC \          -> OSC (title, hyperlinks)
        //   ESC X                          -> single-char intermediate (7, 8, c, =, >, D, M, ...)
        char *r = sink_buf, *w = sink_buf;
        while (*r) {
            if (*r != '\033') { *w++ = *r++; continue; }
            r++;  // consume ESC
            if (!*r) break;           // lone trailing ESC
            char next = *r++;
            if (next == '[') {
                while (*r && !(*r >= 0x40 && *r <= 0x7E)) r++;
                if (*r) r++;
            } else if (next == ']') {
                while (*r) {
                    if (*r == 0x07) { r++; break; }
                    if (*r == '\033' && *(r+1) == '\\') { r += 2; break; }
                    r++;
                }
            }
            // else: ESC X single-char sequence — next byte already consumed
        }
        *w = '\0';

        // Ring buffer sink (always, including SOURCE_MQTT)
        if (log_ring_base) {
            uint32_t slot_addr = log_ring_base + (uint32_t)log_ring_head * LOG_ENTRY_SIZE;
            int slen = strlen(sink_buf);
            if (slen >= LOG_ENTRY_SIZE) slen = LOG_ENTRY_SIZE - 1;
            psram_write(slot_addr, (const uint8_t *)sink_buf, slen);
            psram_write8(slot_addr + slen, 0);  // null terminator
            log_ring_head = (log_ring_head + 1) % log_ring_slots;
            if (log_ring_count < log_ring_slots) log_ring_count++;
        }

        // File sink (always, including SOURCE_MQTT)
        if (log_file) {
            fprintf(log_file, "%s\n", sink_buf);
            fflush(log_file);
        }

        // MQTT sink (skip SOURCE_MQTT to prevent infinite feedback loop).
        // Only STAGE it here — the actual publish happens after the mutex is
        // released. See below.
        if (source != SOURCE_MQTT && mqtt_connected()) {
            snprintf(mqtt_topic, sizeof(mqtt_topic), "conez/%d/debug", config.cone_id);
            mqtt_pending = true;
        }
    }

    // Redraw the command line after our output
    if (console_output)
        shell.resumeLine(OutputStream);

    //return mutex
    xSemaphoreGive(print_mutex);

    // ---- MQTT sink, OUTSIDE the lock ----
    //
    // mqtt_publish() calls esp_mqtt_client_publish(), which blocks on esp-mqtt's
    // internal api_lock — a lock the MQTT task holds across reconnect attempts and
    // socket writes. Publishing while holding print_mutex therefore deadlocks the
    // entire console: getLock() (shell input), every printfnl(), and every debug
    // sink all wait on print_mutex forever. The board keeps answering ping (lwIP
    // never prints) while the CLI is dead — which is exactly how this presented.
    //
    // Triggered in practice by a duplicate-client-id reconnect storm: several cones
    // sharing cone_id 0 evict each other from the broker, so the MQTT task is almost
    // always mid-reconnect.
    if (mqtt_pending) {
        static volatile bool in_mqtt_debug = false;   // guard against re-entry via a printfnl inside the publish path
        if (!in_mqtt_debug) {
            in_mqtt_debug = true;
            mqtt_publish(mqtt_topic, sink_buf);
            in_mqtt_debug = false;
        }
    }
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

void setStream(ConezStream *stream)
{
    getLock();
    if (stream != NULL) 
        OutputStream = stream;
    releaseLock();    
}

ConezStream* getStream(void)
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
    threadLoopCount[thread] = threadLoopCount[thread] + 1;
}


long get_thread_count(int thread)
{
    if (thread < 0 || thread >= 4) {
        return 0; // Invalid thread index
    }
    return threadLoopCount[thread];
}


// ---- Debug log functions ----

void log_init(void)
{
    log_ring_slots = psram_available() ? 64 : 16;
    log_ring_base = psram_malloc(log_ring_slots * LOG_ENTRY_SIZE);
    log_ring_head = 0;
    log_ring_count = 0;
    if (log_ring_base) {
        // Zero first byte of each slot (marks empty)
        for (int i = 0; i < log_ring_slots; i++) {
            psram_write8(log_ring_base + (uint32_t)i * LOG_ENTRY_SIZE, 0);
        }
    }
}

// The ring/file sinks run inside vprintfnl() under print_mutex from any task.
// These teardown/setup paths run on ShellTask, so they MUST take the same lock
// or a concurrent sink can write to a just-freed ring (and divide by a zeroed
// log_ring_slots) or fprintf/fclose a FILE* being closed underneath it.
void log_free(void)
{
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    if (log_ring_base) {
        psram_free(log_ring_base);
        log_ring_base = 0;
        log_ring_slots = 0;
        log_ring_head = 0;
        log_ring_count = 0;
    }
    xSemaphoreGive(print_mutex);
}

bool log_open(const char *path)
{
    char fpath[128];
    lfs_path(fpath, sizeof(fpath), path);
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    log_file = fopen(fpath, "a");
    bool ok = (log_file != NULL);
    xSemaphoreGive(print_mutex);
    return ok;
}

void log_close(void)
{
    xSemaphoreTake(print_mutex, portMAX_DELAY);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    xSemaphoreGive(print_mutex);
}

bool log_save(const char *path)
{
    if (!log_ring_base || log_ring_count == 0) return false;

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), path);
    FILE *f = fopen(fpath, "w");
    if (!f) return false;

    int start = (log_ring_count < log_ring_slots) ? 0 : log_ring_head;
    char entry[LOG_ENTRY_SIZE];

    for (int i = 0; i < log_ring_count; i++) {
        int idx = (start + i) % log_ring_slots;
        uint32_t addr = log_ring_base + (uint32_t)idx * LOG_ENTRY_SIZE;
        psram_read(addr, (uint8_t *)entry, LOG_ENTRY_SIZE);
        entry[LOG_ENTRY_SIZE - 1] = '\0';
        if (entry[0]) {
            int len = strlen(entry);
            if (len > 0 && entry[len - 1] == '\n') entry[len - 1] = '\0';
            fprintf(f, "%s\n", entry);
        }
    }
    fclose(f);
    return true;
}

void log_show(void)
{
    if (!log_ring_base || log_ring_count == 0) {
        printfnl(SOURCE_COMMANDS, "Log buffer empty\n");
        return;
    }

    // Start from oldest entry
    // Use SOURCE_NONE so displayed entries don't get re-captured by sinks
    int start = (log_ring_count < log_ring_slots) ? 0 : log_ring_head;
    char entry[LOG_ENTRY_SIZE];

    for (int i = 0; i < log_ring_count; i++) {
        int idx = (start + i) % log_ring_slots;
        uint32_t addr = log_ring_base + (uint32_t)idx * LOG_ENTRY_SIZE;
        psram_read(addr, (uint8_t *)entry, LOG_ENTRY_SIZE);
        entry[LOG_ENTRY_SIZE - 1] = '\0';
        if (entry[0]) {
            // Strip trailing newline if present (callers' format strings include \n)
            int len = strlen(entry);
            if (len > 0 && entry[len - 1] == '\n') entry[len - 1] = '\0';
            printfnl(SOURCE_NONE, "%s\n", entry);
        }
    }
}
