#include <Arduino.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "conez_wifi.h"
#include "driver/gpio.h"
#include "main.h"
#include "gps.h"
#include "printManager.h"
#include "config.h"

// --- Shared time state (both builds) ---
// Protected by spinlock for 64-bit coherency on 32-bit Xtensa
static portMUX_TYPE time_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t epoch_at_pps = 0;      // epoch ms at last PPS/NTP update
static volatile uint32_t millis_at_pps = 0;     // uptime_ms() at that same moment
static volatile bool     epoch_valid = false;
static volatile uint8_t  time_source = 0;        // 0=none, 1=NTP, 2=GPS+PPS
static volatile uint32_t ntp_last_sync = 0;      // uptime_ms() at last NTP sync (0=never)

// --- SNTP sync callback (fires on LWIP thread when NTP syncs) ---
static void ntp_sync_cb(struct timeval *tv)
{
    uint64_t ep = (uint64_t)tv->tv_sec * 1000ULL + (uint64_t)(tv->tv_usec / 1000);
    uint32_t now_m = uptime_ms();

    // Compute drift from previous time estimate
    int64_t drift_ms = 0;
    portENTER_CRITICAL(&time_mux);
    if (epoch_valid) {
        uint64_t old_ep = epoch_at_pps + (now_m - millis_at_pps);
        drift_ms = (int64_t)ep - (int64_t)old_ep;
    }
    epoch_at_pps = ep;
    millis_at_pps = now_m;
    epoch_valid = true;
    if (time_source < 1) time_source = 1;
    ntp_last_sync = now_m;
    portEXIT_CRITICAL(&time_mux);

    if (drift_ms != 0) {
        printfnl(SOURCE_SYSTEM, "NTP synced (drift %+lld ms)\n", (long long)drift_ms);
    } else {
        printfnl(SOURCE_SYSTEM, "NTP synced (first sync)\n");
    }
}

// --- Compile-time seed ---
// BUILD_EPOCH_S is the UTC epoch at build time, computed by PlatformIO
// (see platformio.ini). Called early in setup() so get_epoch_ms() returns
// a reasonable value even before NTP or GPS are available.
void time_seed_compile(void)
{
#ifdef BUILD_EPOCH_S
    uint64_t ep = (uint64_t)BUILD_EPOCH_S * 1000ULL;
    if (ep == 0) return;

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = uptime_ms();
    epoch_valid = true;
    // time_source stays 0 — NTP (1) and GPS (2) will override
    portEXIT_CRITICAL(&time_mux);
#endif
}

#ifdef BOARD_HAS_GPS

#include "driver/uart.h"
#include "nmea.h"

// Origin coordinates — set from config in gps_setup()
float origin_lat;
float origin_lon;

// Stuff we're exporting
// Marked volatile for cross-core visibility (Core 1 writes, Core 0 reads).
// Individual aligned 32-bit reads/writes are atomic on Xtensa.
volatile float gps_lat;
volatile float gps_lon;
volatile bool gps_pos_valid = false;

volatile float gps_alt = 0;       // Altitude is in meters
volatile bool gps_alt_valid = false;
volatile float gps_dir = 0;
volatile float gps_speed = 0;    // Speed is in m/s

volatile int gps_day = 0;
volatile int gps_month = 0;
volatile int gps_year = 0;
volatile int gps_hour = 0;
volatile int gps_minute = 0;
volatile int gps_second = 0;

// --- PPS interrupt state ---
static volatile uint32_t pps_millis = 0;     // uptime_ms() captured in ISR
static volatile uint32_t pps_count = 0;      // increments each PPS edge
static volatile bool     pps_edge_flag = false; // rising-edge flag, clear-on-read

static nmea_data_t nmea;
static uint32_t nmea_last_update_ms = 0;     // uptime_ms() at last location commit
static uint32_t nmea_last_update_count = 0;  // tracks nmea.update_count

#define GPS_UART     UART_NUM_0
#define GPS_UART_BUF 256


// --- PPS interrupt handler ---
static void IRAM_ATTR pps_isr(void *arg)
{
    (void)arg;
    pps_millis = uptime_ms();
    pps_count++;
    portENTER_CRITICAL_ISR(&time_mux);
    pps_edge_flag = true;
    portEXIT_CRITICAL_ISR(&time_mux);
}


// Convert date/time fields to Unix epoch in milliseconds (UTC)
static uint64_t datetime_to_epoch_ms(int year, int month, int day,
                                     int hour, int minute, int second)
{
    // Days from 1970-01-01 to start of given year
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }

    // Days in each month for the target year
    static const int mdays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    for (int m = 1; m < month; m++) {
        days += mdays[m - 1];
        if (m == 2 && leap) days++;
    }
    days += day - 1;

    uint64_t secs = (uint64_t)days * 86400ULL
                  + (uint64_t)hour * 3600ULL
                  + (uint64_t)minute * 60ULL
                  + (uint64_t)second;
    return secs * 1000ULL;
}


void pps_isr_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << GPS_PPS_PIN);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)GPS_PPS_PIN, pps_isr, NULL);
}


void gps_send_nmea(const char *body)
{
    // Compute XOR checksum of all chars in body (between $ and *)
    uint8_t cs = 0;
    for (const char *p = body; *p; p++)
        cs ^= (uint8_t)*p;
    char msg[128];
    snprintf(msg, sizeof(msg), "$%s*%02X\r\n", body, cs);
    uart_write_bytes(GPS_UART, msg, strlen(msg));
    printfnl(SOURCE_GPS, "Sent: %s", msg);
}


int gps_setup()
{
    // Load origin from config
    origin_lat = config.origin_lat;
    origin_lon = config.origin_lon;
    gps_lat    = origin_lat;
    gps_lon    = origin_lon;

    nmea_init(&nmea);

    // Setup PPS pin and attach interrupt for sub-ms timing
    pps_isr_init();

    uart_config_t uart_cfg = {};
    uart_cfg.baud_rate  = 9600;
    uart_cfg.data_bits  = UART_DATA_8_BITS;
    uart_cfg.parity     = UART_PARITY_DISABLE;
    uart_cfg.stop_bits  = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_APB;
    uart_param_config(GPS_UART, &uart_cfg);
    uart_set_pin(GPS_UART, GPS_TX_PIN, GPS_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(GPS_UART, GPS_UART_BUF, 0, 0, NULL, 0);

    return 0;
}


int gps_loop()
{
    // Buffer for raw NMEA debug output — flush complete lines via printfnl
    // so suspendLine/resumeLine protect the shell prompt.
    static char raw_buf[96];
    static int  raw_pos = 0;

    // Read available bytes from GPS UART in bulk
    uint8_t rxbuf[64];
    int rxlen;
    while ((rxlen = uart_read_bytes(GPS_UART, rxbuf, sizeof(rxbuf), 0)) > 0) {
        for (int i = 0; i < rxlen; i++) {
            unsigned char ch = rxbuf[i];

            if( getDebug( SOURCE_GPS_RAW ) )
            {
                if (raw_pos < (int)sizeof(raw_buf) - 1)
                    raw_buf[raw_pos++] = ch;
                if (ch == '\n' || raw_pos >= (int)sizeof(raw_buf) - 1) {
                    raw_buf[raw_pos] = '\0';
                    printfnl(SOURCE_GPS_RAW, "%s", raw_buf);
                    raw_pos = 0;
                }
            }

            nmea_encode(&nmea, ch);

            // Check for new location update (update_count incremented by parser)
            if (nmea.update_count != nmea_last_update_count)
            {
                nmea_last_update_count = nmea.update_count;
                nmea_last_update_ms = uptime_ms();

                gps_lat = nmea.lat;
                gps_lon = nmea.lon;
                gps_pos_valid = nmea.location_valid;

                gps_alt = nmea.alt;
                gps_alt_valid = nmea.altitude_valid;
                gps_speed = nmea.speed;
                gps_dir = nmea.course;

                gps_day = nmea.day;
                gps_month = nmea.month;
                gps_year = nmea.year;
                gps_hour = nmea.hour;
                gps_minute = nmea.minute;
                gps_second = nmea.second;

                // Compute epoch from NMEA time and anchor to last PPS edge
                if (pps_count > 0 && nmea.date_valid && nmea.time_valid) {
                    uint64_t ep = datetime_to_epoch_ms(gps_year, gps_month, gps_day,
                                                        gps_hour, gps_minute, gps_second);
                    uint32_t pm = pps_millis;  // snapshot atomic 32-bit read
                    portENTER_CRITICAL(&time_mux);
                    epoch_at_pps = ep;
                    millis_at_pps = pm;
                    epoch_valid = true;
                    time_source = 2;  // GPS+PPS — highest priority
                    portEXIT_CRITICAL(&time_mux);
                }

                int date_raw = nmea.date_valid ? nmea.day * 10000 + nmea.month * 100 + (nmea.year % 100) : -1;
                int time_raw = nmea.time_valid ? nmea.hour * 10000 + nmea.minute * 100 + nmea.second : -1;
                printfnl( SOURCE_GPS, "GPS updated: valid=%u  lat=%0.6f  lon=%0.6f  alt=%dm  date=%d  time=%d\n",
                    (int) gps_pos_valid,
                    gps_lat,
                    gps_lon,
                    (int)gps_alt,
                    date_raw,
                    time_raw );
            }
        }
    }

    // Clear fix validity when GPS data goes stale (>10s without update)
    if (gps_pos_valid && (uptime_ms() - nmea_last_update_ms) > 10000) {
        gps_pos_valid = false;
    }

    return 0;
}

float get_lat(void)
{
    return gps_lat;
}

float get_lon(void)
{
    return gps_lon;
}

int get_sec(void)
{
    return gps_second;
}

float get_alt(void)
{
    return gps_alt;
}

float get_speed(void)
{
    return gps_speed;
}

float get_dir(void)
{
    return gps_dir;
}

bool get_gpsstatus(void)
{
    return gps_pos_valid;
}

float get_org_lat(void)
{
    return origin_lat; // Assuming origin is the same as current position
}

float get_org_lon(void)
{
    return origin_lon;
}

int get_day(void)
{
    return gps_day;
}

int get_month(void)
{
    return gps_month;
}

int get_year(void)
{
    return gps_year;
}

int get_hour(void)
{
    return gps_hour;
}

int get_minute(void)
{
    return gps_minute;
}

int get_second(void)
{
    return gps_second;
}


int get_day_of_week(void)
{
    int month = gps_month;
    int year = gps_year;

    if (month < 3) {
        month += 12;
        year -= 1;
    }

    int k = year % 100;
    int j = year / 100;

    int h = (gps_day + (13 * (month + 1)) / 5 + k + k/4 + j/4 + 5*j) % 7;

    // Zeller's congruence: 0=Saturday, so we convert to 0=Sunday
    int day_of_week = (h + 6) % 7;
    return day_of_week;
}

bool get_isleapyear(void)
{
    int year = gps_year;
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int get_dayofyear(void)
{
    int year = gps_year;
    int month = gps_month;
    int day = gps_day;


    // Validate month range
    if (month < 1 || month > 12) {
        return -1; // Invalid month
    }

    // Days in each month for a non-leap year
    int days_in_month[] = { 31, 28, 31, 30, 31, 30,
                            31, 31, 30, 31, 30, 31 };

    if (get_isleapyear()) {
        days_in_month[1] = 29; // February in a leap year
    }

    // Validate day for the given month
    if (day < 1 || day > days_in_month[month - 1]) {
        return -1; // Invalid day
    }

    int doy = 0;
    for (int i = 0; i < month - 1; ++i) {
        doy += days_in_month[i];
    }
    doy += day;

    return doy;
}


int get_satellites(void)
{
    return nmea.satellites;
}

int get_hdop(void)
{
    return nmea.hdop;
}

int get_fix_type(void)
{
    return nmea.fix_type;
}

float get_pdop(void)
{
    return nmea.pdop;
}

float get_vdop(void)
{
    return nmea.vdop;
}

int get_date_raw(void)
{
    if (!nmea.date_valid) return -1;
    return gps_day * 10000 + gps_month * 100 + (gps_year % 100);
}

int get_time_raw(void)
{
    if (!nmea.time_valid) return -1;
    return gps_hour * 10000 + gps_minute * 100 + gps_second;
}

bool get_pps(void)
{
    return gpio_get_level((gpio_num_t)GPS_PPS_PIN) == 1;
}


bool get_pps_flag(void)
{
    portENTER_CRITICAL(&time_mux);
    bool flag = pps_edge_flag;
    pps_edge_flag = false;
    portEXIT_CRITICAL(&time_mux);
    return flag;
}

uint32_t get_pps_age_ms(void)
{
    if (pps_count == 0) return UINT32_MAX;  // never received
    return uptime_ms() - pps_millis;
}

uint32_t get_pps_count(void)
{
    return pps_count;
}


bool get_time_valid(void)
{
    return epoch_valid;
}


uint64_t get_epoch_ms(void)
{
    uint64_t ep;
    uint32_t mp;
    bool valid;

    portENTER_CRITICAL(&time_mux);
    ep = epoch_at_pps;
    mp = millis_at_pps;
    valid = epoch_valid;
    portEXIT_CRITICAL(&time_mux);

    if (!valid) return 0;

    uint32_t elapsed = uptime_ms() - mp;  // unsigned wraps correctly at 49 days
    return ep + elapsed;
}


uint8_t get_time_source(void)
{
    return time_source;
}

uint32_t get_ntp_last_sync_ms(void)
{
    return ntp_last_sync;
}


// NTP on GPS boards: provides time before GPS lock, NTP only wins if GPS+PPS hasn't set epoch yet
void ntp_setup(void)
{
    sntp_set_sync_interval((uint32_t)config.ntp_interval * 1000);
    sntp_set_time_sync_notification_cb(ntp_sync_cb);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, config.ntp_server);
    sntp_setservername(1, "time.nist.gov");
    sntp_init();
}


void ntp_loop(void)
{
    // Auto-initialize SNTP when WiFi connects (handles CLI wifi commands, reconnects)
    if (wifi_is_connected() && !sntp_enabled()) {
        ntp_setup();
    }

    // If GPS+PPS is active, only allow NTP fallback if GPS is stale (>10s)
    if (time_source >= 2) {
        portENTER_CRITICAL(&time_mux);
        uint32_t mp = millis_at_pps;
        portEXIT_CRITICAL(&time_mux);
        if (uptime_ms() - mp < 10000) return;  // GPS still fresh
        // GPS stale — downgrade so NTP can fill in
        portENTER_CRITICAL(&time_mux);
        time_source = 0;
        portEXIT_CRITICAL(&time_mux);
    }

    // Rate limit: update once per second
    static uint32_t last_ntp_ms = 0;
    if (uptime_ms() - last_ntp_ms < 1000) return;
    last_ntp_ms = uptime_ms();

    struct timeval tv;
    gettimeofday(&tv, NULL);

    // ESP32 SNTP sets the system clock; check if it's been set (> year 2024)
    if (tv.tv_sec < 1704067200L) {
        // NTP not available — keep time fields advancing from free-running epoch
        if (epoch_valid) {
            uint64_t ep = get_epoch_ms();
            time_t t = (time_t)(ep / 1000);
            struct tm tm;
            gmtime_r(&t, &tm);
            gps_year   = tm.tm_year + 1900;
            gps_month  = tm.tm_mon + 1;
            gps_day    = tm.tm_mday;
            gps_hour   = tm.tm_hour;
            gps_minute = tm.tm_min;
            gps_second = tm.tm_sec;
        }
        return;
    }

    uint64_t ep = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
    uint32_t now_m = uptime_ms();

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = now_m;
    epoch_valid = true;
    // Don't promote time_source here — only ntp_sync_cb() should set it to 1.
    // The system clock may be valid from RTC retention across soft resets,
    // not from an actual NTP sync this session.
    portEXIT_CRITICAL(&time_mux);

    // Populate date/time volatiles so existing getters work even before GPS fix
    struct tm tm;
    time_t t = tv.tv_sec;
    gmtime_r(&t, &tm);
    gps_year   = tm.tm_year + 1900;
    gps_month  = tm.tm_mon + 1;
    gps_day    = tm.tm_mday;
    gps_hour   = tm.tm_hour;
    gps_minute = tm.tm_min;
    gps_second = tm.tm_sec;
}


#else // No GPS hardware — NTP provides time

// Date/time volatiles populated by NTP so existing getters work
static volatile int gps_day = 0;
static volatile int gps_month = 0;
static volatile int gps_year = 0;
static volatile int gps_hour = 0;
static volatile int gps_minute = 0;
static volatile int gps_second = 0;

int gps_setup() { return 0; }
int gps_loop() { return 0; }
void pps_isr_init(void) { }
void gps_send_nmea(const char *) { }

float get_lat(void) { return 0; }
float get_lon(void) { return 0; }
int get_sec(void) { return gps_second; }
float get_alt(void) { return 0; }
float get_speed(void) { return 0; }
float get_dir(void) { return 0; }
bool get_gpsstatus(void) { return false; }
float get_org_lat(void) { return 0; }
float get_org_lon(void) { return 0; }

int get_day(void) { return gps_day; }
int get_month(void) { return gps_month; }
int get_year(void) { return gps_year; }
int get_hour(void) { return gps_hour; }
int get_minute(void) { return gps_minute; }
int get_second(void) { return gps_second; }

int get_day_of_week(void)
{
    int month = gps_month;
    int year = gps_year;
    if (month < 3) { month += 12; year -= 1; }
    int k = year % 100;
    int j = year / 100;
    int h = (gps_day + (13 * (month + 1)) / 5 + k + k/4 + j/4 + 5*j) % 7;
    return (h + 6) % 7;
}

int get_dayofyear(void)
{
    if (gps_month < 1 || gps_month > 12) return -1;
    int days_in_month[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    bool leap = (gps_year % 4 == 0 && gps_year % 100 != 0) || (gps_year % 400 == 0);
    if (leap) days_in_month[1] = 29;
    if (gps_day < 1 || gps_day > days_in_month[gps_month - 1]) return -1;
    int doy = 0;
    for (int i = 0; i < gps_month - 1; ++i) doy += days_in_month[i];
    return doy + gps_day;
}

bool get_isleapyear(void)
{
    return (gps_year % 4 == 0 && gps_year % 100 != 0) || (gps_year % 400 == 0);
}

int get_date_raw(void) { return -1; }
int get_time_raw(void) { return -1; }
int get_satellites(void) { return 0; }
int get_hdop(void) { return 0; }
int get_fix_type(void) { return 0; }
float get_pdop(void) { return 0; }
float get_vdop(void) { return 0; }
bool get_pps(void) { return false; }
bool get_pps_flag(void) { return false; }
uint32_t get_pps_age_ms(void) { return UINT32_MAX; }
uint32_t get_pps_count(void) { return 0; }


bool get_time_valid(void)
{
    return epoch_valid;
}


uint64_t get_epoch_ms(void)
{
    uint64_t ep;
    uint32_t mp;
    bool valid;

    portENTER_CRITICAL(&time_mux);
    ep = epoch_at_pps;
    mp = millis_at_pps;
    valid = epoch_valid;
    portEXIT_CRITICAL(&time_mux);

    if (!valid) return 0;

    uint32_t elapsed = uptime_ms() - mp;
    return ep + elapsed;
}


uint8_t get_time_source(void)
{
    return time_source;
}

uint32_t get_ntp_last_sync_ms(void)
{
    return ntp_last_sync;
}


void ntp_setup(void)
{
    sntp_set_sync_interval((uint32_t)config.ntp_interval * 1000);
    sntp_set_time_sync_notification_cb(ntp_sync_cb);
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, config.ntp_server);
    sntp_setservername(1, "time.nist.gov");
    sntp_init();
}


void ntp_loop(void)
{
    // Auto-initialize SNTP when WiFi connects (handles CLI wifi commands, reconnects)
    if (wifi_is_connected() && !sntp_enabled()) {
        ntp_setup();
    }

    // Rate limit: update once per second
    static uint32_t last_ntp_ms = 0;
    if (uptime_ms() - last_ntp_ms < 1000) return;
    last_ntp_ms = uptime_ms();

    struct timeval tv;
    gettimeofday(&tv, NULL);

    // ESP32 SNTP sets the system clock; check if it's been set (> year 2024)
    if (tv.tv_sec < 1704067200L) return;  // 2024-01-01 00:00:00 UTC

    uint64_t ep = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
    uint32_t now_m = uptime_ms();

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = now_m;
    epoch_valid = true;
    // Don't promote time_source here — only ntp_sync_cb() should set it to 1.
    // The system clock may be valid from RTC retention across soft resets.
    portEXIT_CRITICAL(&time_mux);

    // Populate date/time volatiles so existing getters work
    struct tm tm;
    time_t t = tv.tv_sec;
    gmtime_r(&t, &tm);
    gps_year   = tm.tm_year + 1900;
    gps_month  = tm.tm_mon + 1;
    gps_day    = tm.tm_mday;
    gps_hour   = tm.tm_hour;
    gps_minute = tm.tm_min;
    gps_second = tm.tm_sec;
}

#endif
