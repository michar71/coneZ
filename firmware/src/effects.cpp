#include <Arduino.h>
#include "main.h"
#include "util.h"
#include "gps.h"


extern Stream *OutputStream;


void SOS_effect(void)
{
    //Get Lat/Lon/Time
    float lat = get_lat();
    float lon = get_lon();
    int sec = get_sec();
    static int prev_sec = 0;

    float origin_lat = 40.762173;
    float origin_lon = -119.193672;

    //Calulate Offset From Equator/0-meridian in Meters
    float lat_m;
    float lon_m;
    float lat_o_m;
    float lon_o_m;
    float dist_meters;

    //Calulate distance from Origin
    latlon_to_meters(lat, lon,&lat_m,&lon_m);
    latlon_to_meters(origin_lat, origin_lon,&lat_o_m,&lon_o_m);
    GeoResult res = xy_to_polar(lat_m,lon_m, lat_o_m,lon_o_m);
    dist_meters = res.distance;

    Serial.print("Dist: ");
    Serial.println(dist_meters);

    //calulate offset in MS wth speed ofd sound being 343m/s
    float sos_ms = 343.0;
    
    float offset_ms = dist_meters / sos_ms * 1000;
    OutputStream->print("Offset ");
    OutputStream->println(offset_ms);

    OutputStream->print("sec = ");
    OutputStream->println( sec );

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec && sec%10 == 0)
    {
        prev_sec = sec;

        //Wait Offset MS
        delay((int)round(offset_ms));
        OutputStream->print("PING - sec = ");
        OutputStream->println( sec );
      
        //Flash Light
        for (int ii = 255; ii>0; ii=ii-32)
        {
          CRGB col;
          col.r = ii;
          col.g = ii;
          col.b = ii;
          color_leds(1, 50, col);
          delay (20);
        }
        color_leds(1, 50, CRGB::Black);
        delay(25);
        color_leds(1, 50, CRGB::Black);
        //Wait for 1 sec so we don't do it twice...
        //delay(3000);
      }
}



void SOS_effect2(void)
{
    CRGB col;

    //Get Lat/Lon/Time
    float lat = get_lat();
    float lon = get_lon();
    int sec = get_sec();
    static int prev_sec = 0;

    float origin_lat = 40.762173;
    float origin_lon = -119.193672;

    //Calulate Offset From Equator/0-meridian in Meters
    float lat_m;
    float lon_m;
    float lat_o_m;
    float lon_o_m;
    float dist_meters;

    //Calulate distance from Origin
    latlon_to_meters(lat, lon,&lat_m,&lon_m);
    latlon_to_meters(origin_lat, origin_lon,&lat_o_m,&lon_o_m);
    GeoResult res = xy_to_polar(lat_m,lon_m, lat_o_m,lon_o_m);
    dist_meters = res.distance;

    Serial.print("Dist: ");
    Serial.println(dist_meters);

    //calulate offset in MS wth speed ofd sound being 343m/s
    float sos_ms = 343.0;
    
    float offset_ms = dist_meters / sos_ms * 1000;
    OutputStream->print("Offset ");
    OutputStream->println(offset_ms);

    OutputStream->print("sec = ");
    OutputStream->println( sec );

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec && sec%5 == 0)
    {
        prev_sec = sec;

        //Wait Offset MS
        delay((int)round(offset_ms));
        OutputStream->print("PING - sec = ");
        OutputStream->println( sec );
      
        //Flash Light
        for (int ii = 255; ii>0; ii=ii-16)
        {
          CRGB col;
          col.r = ii;
          col.g = ii;
          col.b = ii;
          color_leds(1, 50, col);
          delay (20);
        }

        // Baseline green glow
        col.r = 0;
        col.g = 5;
        col.b = 0;
        color_leds(1, 50, col );
        delay(25);
        color_leds(1, 50, col );
        delay( 25 );
        //Wait for 1 sec so we don't do it twice...
        //delay(3000);
      }
}
