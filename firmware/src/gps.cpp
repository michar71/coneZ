#include <Arduino.h>
#include "main.h"
#include "gps.h"
#include "printManager.h"

#ifdef BOARD_HAS_GPS

#include <HardwareSerial.h>
#include <TinyGPSPlus.h>

// Stuff that needs to be set via LoRa
//   Nevada desert festival origin:
float origin_lat = 40.762173;
float origin_lon = -119.193672;
//   ESE origin:
//float origin_lat = 36.236735;
//float origin_lon = -118.025373;


// Stuff we're exporting
// Marked volatile for cross-core visibility (Core 1 writes, Core 0 reads).
// Individual aligned 32-bit reads/writes are atomic on Xtensa.
volatile float gps_lat = /*40.76*/ origin_lat;
volatile float gps_lon = /*-119.19*/ origin_lon;
volatile bool gps_pos_valid = false;

volatile float gps_alt = 0;       // Altitude is in meters
volatile bool gps_alt_valid = false;
volatile float gps_dir = 0;
volatile float gps_speed = 0;    // Speed is in m/s

volatile bool gps_time_valid = false;
volatile uint32_t gps_time = 0;

volatile int gps_day = 0;
volatile int gps_month = 0;
volatile int gps_year = 0;
volatile int gps_hour = 0;
volatile int gps_minute = 0;
volatile int gps_second = 0;

TinyGPSPlus gps;

// Serial
HardwareSerial GPSSerial(0);


int gps_setup()
{
    //Setup PPS Pin
    pinMode(GPS_PPS_PIN, INPUT_PULLUP);
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

        if( gps.location.isUpdated() )
        {
            gps_lat = gps.location.lat();
            gps_lon = gps.location.lng();
            gps_pos_valid = gps.location.isValid();

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

            printfnl( SOURCE_GPS, F("GPS updated: valid=%u  lat=%0.6f  lon=%0.6f  alt=%dm  date=%d  time=%d\n"),
                (int) gps_pos_valid,
                gps_lat,
                gps_lon,
                (int)gps_alt,
                gps.date.isValid() ? gps.date.value() : -1,
                gps.time.isValid() ? gps.time.value() : -1 );

            //if( gps.time.isValid() )
            //{
            //    printfnl(SOURCE_GPS,"GPS Time: date=%u  time=%u\n",  gps.date.value(), gps.time.value());
            //}
        }
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

#else // No GPS hardware

int gps_setup() { return 0; }
int gps_loop() { return 0; }

float get_lat(void) { return 0; }
float get_lon(void) { return 0; }
int get_sec(void) { return 0; }
float get_alt(void) { return 0; }
float get_speed(void) { return 0; }
float get_dir(void) { return 0; }
bool get_gpsstatus(void) { return false; }
float get_org_lat(void) { return 0; }
float get_org_lon(void) { return 0; }

int get_day(void) { return 0; }
int get_month(void) { return 0; }
int get_year(void) { return 0; }
int get_hour(void) { return 0; }
int get_minute(void) { return 0; }
int get_second(void) { return 0; }
int get_day_of_week(void) { return 0; }
int get_dayofyear(void) { return 0; }
bool get_isleapyear(void) { return false; }
int get_satellites(void) { return 0; }
int get_hdop(void) { return 0; }
bool get_pps(void) { return false; }

#endif
