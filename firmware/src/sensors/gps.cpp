#include <Arduino.h>
#include <sys/time.h>
#include "main.h"
#include "gps.h"
#include "printManager.h"
#include "config.h"

// --- Shared time state (both builds) ---
// Protected by spinlock for 64-bit coherency on 32-bit Xtensa
static portMUX_TYPE time_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t epoch_at_pps = 0;      // epoch ms at last PPS/NTP update
static volatile uint32_t millis_at_pps = 0;     // millis() at that same moment
static volatile bool     epoch_valid = false;
static volatile uint8_t  time_source = 0;        // 0=none, 1=NTP, 2=GPS+PPS

// --- Compile-time seed ---
// Parse __DATE__ ("Feb 16 2026") and __TIME__ ("13:45:02") into epoch ms.
// Called early in setup() so get_epoch_ms() returns a reasonable value even
// before NTP or GPS are available. NTP (source 1) and GPS+PPS (source 2)
// override this automatically when they connect.
static uint64_t parse_compile_time(void)
{
    const char *date = __DATE__;  // "Mmm dd yyyy"
    const char *time = __TIME__;  // "hh:mm:ss"

    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int mon = 0;
    for (int i = 0; i < 12; i++) {
        if (date[0] == months[i*3] && date[1] == months[i*3+1] && date[2] == months[i*3+2]) {
            mon = i;
            break;
        }
    }

    struct tm tm = {};
    tm.tm_year = atoi(date + 7) - 1900;
    tm.tm_mon  = mon;
    tm.tm_mday = atoi(date + 4);
    tm.tm_hour = atoi(time);
    tm.tm_min  = atoi(time + 3);
    tm.tm_sec  = atoi(time + 6);

    time_t t = mktime(&tm);  // assumes UTC (ESP32 default, no TZ set at boot)
    if (t < 0) return 0;
    return (uint64_t)t * 1000ULL;
}

void time_seed_compile(void)
{
    uint64_t ep = parse_compile_time();
    if (ep == 0) return;

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = millis();
    epoch_valid = true;
    // time_source stays 0 — NTP (1) and GPS (2) will override
    portEXIT_CRITICAL(&time_mux);
}

#ifdef BOARD_HAS_GPS

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

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
static volatile uint32_t pps_millis = 0;     // millis() captured in ISR
static volatile uint32_t pps_count = 0;      // increments each PPS edge
static volatile bool     pps_edge_flag = false; // rising-edge flag, clear-on-read

TinyGPSPlus gps;

// Custom GSA fields (fix type, DOP values)
// GSA: $G?GSA,mode,fixtype,sv1..sv12,PDOP,HDOP,VDOP*cs
//       field: 1    2      3..14     15   16   17
// Register for both GPGSA and GNGSA prefixes
static TinyGPSCustom gsaFixType_GP(gps, "GPGSA", 2);   // 1=no fix, 2=2D, 3=3D
static TinyGPSCustom gsaPDOP_GP(gps, "GPGSA", 15);
static TinyGPSCustom gsaHDOP_GP(gps, "GPGSA", 16);
static TinyGPSCustom gsaVDOP_GP(gps, "GPGSA", 17);
static TinyGPSCustom gsaFixType_GN(gps, "GNGSA", 2);
static TinyGPSCustom gsaPDOP_GN(gps, "GNGSA", 15);
static TinyGPSCustom gsaHDOP_GN(gps, "GNGSA", 16);
static TinyGPSCustom gsaVDOP_GN(gps, "GNGSA", 17);

volatile int gps_fix_type = 0;      // 0=unknown, 1=no fix, 2=2D, 3=3D
volatile float gps_pdop = 0;
volatile float gps_vdop = 0;

// Serial
HardwareSerial GPSSerial(0);


// --- PPS interrupt handler ---
static void IRAM_ATTR pps_isr(void)
{
    pps_millis = millis();
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
    attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), pps_isr, RISING);
}


void gps_send_nmea(const char *body)
{
    // Compute XOR checksum of all chars in body (between $ and *)
    uint8_t cs = 0;
    for (const char *p = body; *p; p++)
        cs ^= (uint8_t)*p;
    char msg[128];
    snprintf(msg, sizeof(msg), "$%s*%02X\r\n", body, cs);
    GPSSerial.print(msg);
    printfnl(SOURCE_GPS, F("Sent: %s"), msg);
}


int gps_setup()
{
    // Load origin from config
    origin_lat = config.origin_lat;
    origin_lon = config.origin_lon;
    gps_lat    = origin_lat;
    gps_lon    = origin_lon;

    //Setup PPS Pin
    pinMode(GPS_PPS_PIN, INPUT_PULLUP);

    // Attach PPS interrupt for sub-ms timing
    pps_isr_init();

    GPSSerial.begin( 9600, SERIAL_8N1,     // baud, mode, RX-pin, TX-pin
                     GPS_RX_PIN, GPS_TX_PIN );

    return 0;
}


int gps_loop()
{
    // Any characters from the GPS waiting for us?
    while( GPSSerial.available() )
    {
        unsigned char ch = GPSSerial.read();

        if( getDebug( SOURCE_GPS_RAW ) )
        {

            getLock();
            getStream()->write( ch );
            releaseLock();
        }

        gps.encode( ch );

        // Capture GSA fields (fix type, DOP values) from whichever prefix is active
        if (gsaFixType_GP.isUpdated()) gps_fix_type = atoi(gsaFixType_GP.value());
        if (gsaFixType_GN.isUpdated()) gps_fix_type = atoi(gsaFixType_GN.value());
        if (gsaPDOP_GP.isUpdated()) gps_pdop = atof(gsaPDOP_GP.value());
        if (gsaPDOP_GN.isUpdated()) gps_pdop = atof(gsaPDOP_GN.value());
        if (gsaVDOP_GP.isUpdated()) gps_vdop = atof(gsaVDOP_GP.value());
        if (gsaVDOP_GN.isUpdated()) gps_vdop = atof(gsaVDOP_GN.value());

        if( gps.location.isUpdated() )
        {
            gps_lat = gps.location.lat();
            gps_lon = gps.location.lng();
            gps_pos_valid = gps.location.isValid() && gps.location.age() < 10000;

            gps_alt = gps.altitude.meters();
            gps_alt_valid = gps.altitude.isValid();
            gps_speed = gps.speed.mps();
            gps_dir = gps.course.deg();

            gps_day = gps.date.day();
            gps_month = gps.date.month();
            gps_year = gps.date.year();
            gps_hour = gps.time.hour();
            gps_minute = gps.time.minute();
            gps_second = gps.time.second();

            // Compute epoch from NMEA time and anchor to last PPS edge
            if (pps_count > 0 && gps.date.isValid() && gps.time.isValid()) {
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

            printfnl( SOURCE_GPS, F("GPS updated: valid=%u  lat=%0.6f  lon=%0.6f  alt=%dm  date=%d  time=%d\n"),
                (int) gps_pos_valid,
                gps_lat,
                gps_lon,
                (int)gps_alt,
                gps.date.isValid() ? gps.date.value() : -1,
                gps.time.isValid() ? gps.time.value() : -1 );
        }
    }

    // Clear fix validity when GPS data goes stale (>10s without update)
    if (gps_pos_valid && gps.location.age() > 10000) {
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
    return gps.satellites.value();
}

int get_hdop(void)
{
    return gps.hdop.value();
}

int get_fix_type(void)
{
    return gps_fix_type;
}

float get_pdop(void)
{
    return gps_pdop;
}

float get_vdop(void)
{
    return gps_vdop;
}

bool get_pps(void)
{
    if (digitalRead(GPS_PPS_PIN) == HIGH)
    {
        return true;
    }
    else
    {
        return false;
    }
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
    return millis() - pps_millis;
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

    uint32_t elapsed = millis() - mp;  // unsigned wraps correctly at 49 days
    return ep + elapsed;
}


uint8_t get_time_source(void)
{
    return time_source;
}


// NTP on GPS boards: provides time before GPS lock, NTP only wins if GPS+PPS hasn't set epoch yet
void ntp_setup(void)
{
    if (config.ntp_server[0] != '\0')
        configTime(0, 0, config.ntp_server, "pool.ntp.org");
    else
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}


void ntp_loop(void)
{
    // If GPS+PPS is active, only allow NTP fallback if GPS is stale (>10s)
    if (time_source >= 2) {
        portENTER_CRITICAL(&time_mux);
        uint32_t mp = millis_at_pps;
        portEXIT_CRITICAL(&time_mux);
        if (millis() - mp < 10000) return;  // GPS still fresh
        // GPS stale — downgrade so NTP can fill in
        portENTER_CRITICAL(&time_mux);
        time_source = 0;
        portEXIT_CRITICAL(&time_mux);
    }

    // Rate limit: update once per second
    static uint32_t last_ntp_ms = 0;
    if (millis() - last_ntp_ms < 1000) return;
    last_ntp_ms = millis();

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
    uint32_t now_m = millis();

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = now_m;
    epoch_valid = true;
    if (time_source < 1) time_source = 1;
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

    uint32_t elapsed = millis() - mp;
    return ep + elapsed;
}


uint8_t get_time_source(void)
{
    return time_source;
}


void ntp_setup(void)
{
    if (config.ntp_server[0] != '\0')
        configTime(0, 0, config.ntp_server, "pool.ntp.org");
    else
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}


void ntp_loop(void)
{
    // Rate limit: update once per second
    static uint32_t last_ntp_ms = 0;
    if (millis() - last_ntp_ms < 1000) return;
    last_ntp_ms = millis();

    struct timeval tv;
    gettimeofday(&tv, NULL);

    // ESP32 SNTP sets the system clock; check if it's been set (> year 2024)
    if (tv.tv_sec < 1704067200L) return;  // 2024-01-01 00:00:00 UTC

    uint64_t ep = (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
    uint32_t now_m = millis();

    portENTER_CRITICAL(&time_mux);
    epoch_at_pps = ep;
    millis_at_pps = now_m;
    epoch_valid = true;
    if (time_source < 1) time_source = 1;
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
