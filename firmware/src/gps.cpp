#include <Arduino.h>
#include <HardwareSerial.h>
#include "main.h"
#include "gps.h"

// External variables
extern Stream *OutputStream;
extern uint32_t debug;

// Stuff we're exporting
float gps_lat = 39.0;
float gps_lon = -119.0;
float gps_alt = 3900;
bool gps_pos_valid = false;
bool gps_time_valid = false;
uint32_t gps_time = 0;


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

        if( debug & DEBUG_MSG_GPS_RAW )     OutputStream->write( GPSSerial.read() );
    }

    return 0;
}
