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
    GPSSerial.begin( 9600, SERIAL_8N1,     // baud, mode, RX-pin, TX-pin
                     44 /*RX0*/, 43 /*TX0*/ );

    return 0;
}


int gps_loop()
{
    while( GPSSerial.available() )
    {
        unsigned char ch = GPSSerial.read();

        if (getDebug(SOURCE_GPS))
        {

            getLock();
            getStream()->write( ch );
            releaseLock();

        gps.encode( ch );
        }

        if( gps.location.isUpdated() )
        {
            gps_lat = gps.location.lat();
            gps_lon = gps.location.lng();
            gps_pos_valid = gps.location.isValid();

            gps_alt = gps.altitude.meters();
            gps_alt_valid = gps.altitude.isValid();
            gps_speed = gps.speed.mps();
            gps_dir = gps.course.deg();

            printfnl(SOURCE_GPS,"GPS updated: Valid=%u Lat=%0.6f  Lon=%0.6f  Alt=%dm\n", (int) gps_pos_valid, gps_lon, (int)gps_alt);


            if( gps.time.isValid() )
            {
                printfnl(SOURCE_GPS,"GPS Time: date=%u  time=%u\n",  gps.date.value(), gps.time.value());
            }
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
