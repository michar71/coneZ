#include <Arduino.h>
#include <HardwareSerial.h>
#include "main.h"
#include "gps.h"


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
    return 0;
}
