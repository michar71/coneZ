#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include "main.h"
#include "gps.h"

// External variables
extern Stream *OutputStream;
extern uint32_t debug;

// Stuff we're exporting
float gps_lat = 40.76;
float gps_lon = -119.19;
bool gps_pos_valid = false;

float gps_alt = 1234;       // Altitude is in meters
bool gps_alt_valid = false;

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

        if( debug & DEBUG_MSG_GPS_RAW )     OutputStream->write( ch );

        gps.encode( ch );
    }

    if( gps.location.isUpdated() )
    {
        gps_lat = gps.location.lat();
        gps_lon = gps.location.lng();
        gps_pos_valid = gps.location.isValid();

        gps_alt = gps.altitude.meters();
        gps_alt_valid = gps.altitude.isValid();

        if( debug & DEBUG_MSG_GPS )
        {
            OutputStream->print( "<GPS> Update available: " );
            OutputStream->printf( "Valid=%u  Lat=%0.6f  Lon=%0.6f  Alt=%u", (int) gps_pos_valid, gps_lat, gps_lon, gps_alt );
            OutputStream->print( "\n" );
        }


        if( gps.time.isValid() )
        {
            if( debug & DEBUG_MSG_GPS )
            {
                OutputStream->print( "<GPS> Time valid: " );
                OutputStream->printf( "date=%u  time=%u", gps.date.value(), gps.time.value() );
                OutputStream->print( "\n" );
            }
        }
    }

    return 0;
}
