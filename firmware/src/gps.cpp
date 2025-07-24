#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include "main.h"
#include "gps.h"
#include "printManager.h"

//Stuff that needs to be set via LoRa
float origin_lat = 40.762173;
float origin_lon = -119.193672;


// Stuff we're exporting
float gps_lat = 40.76;
float gps_lon = -119.19;
bool gps_pos_valid = false;

float gps_alt = 0;       // Altitude is in meters
bool gps_alt_valid = false;
float gps_dir = 0;
float gps_speed = 0;    // Speed is in m/s

bool gps_time_valid = false;
uint32_t gps_time = 0;

TinyGPSPlus gps;

// Serial
HardwareSerial GPSSerial(0);


int gps_setup()
{
    //Setup PPS Pin
    pinMode(GPS_PPS_PIN, INPUT_PULLUP);
    GPSSerial.begin( 9600, SERIAL_8N1,     // baud, mode, RX-pin, TX-pin
                     44 /*RX0*/, 43 /*TX0*/ );

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
    return gps.time.second();
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
    return gps.date.day();
}   

int get_month(void)
{
    return gps.date.month();
}

int get_year(void)
{
    return gps.date.year();
}

int get_hour(void)
{
    return gps.time.hour();
}

int get_minute(void)
{
    return gps.time.minute();
}

int get_second(void)
{
    return gps.time.second();
}


int get_day_of_week(void)
{
    int month = gps.date.month();
    int year = gps.date.year();

        if (month < 3) {
        month += 12;
        year -= 1;
    }

    int k = year % 100;
    int j = year / 100;

    int h = (gps.date.day() + (13 * (month + 1)) / 5 + k + k/4 + j/4 + 5*j) % 7;

    // Zeller's congruence: 0=Saturday, so we convert to 0=Sunday
    int day_of_week = (h + 6) % 7;
    return day_of_week;
}

bool get_isleapyear(void)
{
    int year = gps.date.year();
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}   

int get_dayofyear(void)
{
    int year = gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();


    // Days in each month for a non-leap year
    int days_in_month[] = { 31, 28, 31, 30, 31, 30,
                            31, 31, 30, 31, 30, 31 };

    if (get_isleapyear()) {
        days_in_month[1] = 29; // February in a leap year
    }

    // Validate day for the given month
    if (day > days_in_month[month - 1]) {
        return -1; // Invalid day
    }

    int doy = 0;
    for (int i = 0; i < month - 1; ++i) {
        doy += days_in_month[i];
    }
    doy += day;

    return doy;
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