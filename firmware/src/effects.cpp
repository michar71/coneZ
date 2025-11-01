#include <Arduino.h>
#include "main.h"
#include "util.h"
#include "gps.h"


extern Stream *OutputStream;
extern bool gps_pos_valid;



//------------------------
//Location-based Functions
//------------------------

#define EARTH_RADIUS_METERS 6378137.0

// Converts latitude and longitude (in degrees) into x/y offsets (in meters)
// from the Equator and Prime Meridian.
// x = east-west offset (longitude), y = north-south offset (latitude)
void latlon_to_meters(float latitude_deg, float longitude_deg,
                      float *x_offset_meters, float *y_offset_meters) {
    // Convert latitude to radians for cosine calculation
    double lat_rad = latitude_deg * (M_PI / 180.0);

    // North-south offset from equator (meters)
    *y_offset_meters = EARTH_RADIUS_METERS * (M_PI / 180.0) * latitude_deg;

    // East-west offset from Prime Meridian (meters), adjusted by latitude
    *x_offset_meters = EARTH_RADIUS_METERS * cos(lat_rad) * (M_PI / 180.0) * longitude_deg;
}


//For large distsnces this is the correct formula
GeoResult calculate_geo(float lat1, float lon1, float lat2, float lon2) {
    GeoResult result;

    // Convert degrees to radians
    float lat1_rad = lat1 * DEG_TO_RAD;
    float lat2_rad = lat2 * DEG_TO_RAD;
    float delta_lat = (lat2 - lat1) * DEG_TO_RAD;
    float delta_lon = (lon2 - lon1) * DEG_TO_RAD;

    // Haversine formula for distance
    float a = sinf(delta_lat / 2.0f) * sinf(delta_lat / 2.0f) +
              cosf(lat1_rad) * cosf(lat2_rad) *
              sinf(delta_lon / 2.0f) * sinf(delta_lon / 2.0f);
    float c = 2.0f * atanf(sqrtf(a) / sqrtf(1.0f - a));
    result.distance = EARTH_RADIUS_METERS * c;

    // Formula for initial bearing
    float y = sinf(delta_lon) * cosf(lat2_rad);
    float x = cosf(lat1_rad) * sinf(lat2_rad) -
              sinf(lat1_rad) * cosf(lat2_rad) * cosf(delta_lon);
    float bearing_rad = atan2f(y, x);
    float bearing_deg = bearing_rad * RAD_TO_DEG;

    // Normalize to 0–360
    if (bearing_deg < 0.0f) {
        bearing_deg += 360.0f;
    }
    result.bearing_deg = bearing_deg;

    return result;
}

//For small distances or flat-earthers this is totally fine (Ignoring that earth is a sphere)
GeoResult xy_to_polar(float x1, float y1, float x2, float y2) 
{
    GeoResult result;

    float dx = x2 - x1;
    float dy = y2 - y1;

    result.distance = sqrtf(dx * dx + dy * dy);
    float angle_rad = atan2f(dy, dx);
    float angle_deg = angle_rad * (180.0f / 3.14159265f);

    // Normalize to 0–360 degrees
    if (angle_deg < 0.0f)
        angle_deg += 360.0f;

    result.bearing_deg = angle_deg;
    return result;
}


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
      }
}



void SOS_effect2(void)
{
    CRGB col;
    int ms_per_cycle = 3000;
    float sos_speed_scaling = 0.5;

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

    // Calulate offset in ms with speed of sound being approximately 343m/s
    const float sos_mps = 343.0 * sos_speed_scaling;
    
    float offset_ms = dist_meters / sos_mps * 1000;
 
    offset_ms = fmod( offset_ms, ms_per_cycle );
 
    OutputStream->print("Offset ");
    OutputStream->println(offset_ms);

    OutputStream->print("sec = ");
    OutputStream->println( sec );

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec && sec % 3 == 0)
    {
        prev_sec = sec;

        //Wait Offset MS
        delay((int)round(offset_ms));
        OutputStream->print("PING - sec = ");
        OutputStream->println( sec );
      
        //Flash Light - Ramp up
        for (int ii = 0; ii<255; ii=ii+16)
        {
          CRGB col;
          col.r = ii;
          col.g = ii;
          col.b = ii;
          color_leds(1, 50, col);
          delay (20);
        }

        //Flash Light - Ramp down
        for (int ii = 255; ii>0; ii=ii-8)
        {
          CRGB col;
          col.r = ii;
          col.g = ii;
          col.b = ii;
          color_leds(1, 50, col);
          delay (20);
        }

        // Baseline green glow (or blue if not valid GPS)
        if( gps_pos_valid )
        {
          col.r = 0;
          col.g = 4;
          col.b = 0;
        }
        else
        {
            col.r = 0;
            col.g = 0;
            col.b = 10;
        }
        color_leds( 1, 50, col );
        delay( 20 );
        color_leds( 1, 50, col );
        delay( 20 );
      }
}

//Set cones up in a circle around camp
//Each cone determines its angle in deg in relationship to base
//Each cone picks its color based on 360 deg mapped to 255 hue
//all cones sinc on 5 seconds rollover
//each cone determines its offset in deg * 10 in ms 
void CIRCLE_effect(void)
{
    CRGB col;

    //Get Lat/Lon/Time
    float lat = get_lat();
    float lon = get_lon();
    int sec = get_sec();
    static int prev_sec = 0;
    static int offset_cnt = 0;

    float origin_lat = 40.762173;
    float origin_lon = -119.193672;

    //Calulate Offset From Equator/0-meridian in Meters
    float lat_m;
    float lon_m;
    float lat_o_m;
    float lon_o_m;
    float deg;

    //Calulate distance from Origin
    latlon_to_meters(lat, lon,&lat_m,&lon_m);
    latlon_to_meters(origin_lat, origin_lon,&lat_o_m,&lon_o_m);
    GeoResult res = xy_to_polar(lat_m,lon_m, lat_o_m,lon_o_m);
    deg = res.bearing_deg;
    int offset_ms = (int)(deg * 10);

    Serial.print("Deg: ");
    Serial.println(deg);

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec /*&& sec%2 == 0*/ )
    {
        prev_sec = sec;

        //Wait Offset MS
        delay((int)round(offset_ms));
        OutputStream->print("PING - sec = ");
        OutputStream->println( sec );
      
        int hue = (int)map(deg,0,360,0,255);
        //hue = hue + offset_cnt;
        hue = hue + map( get_sec(), 0, 59, 0, 255 );
        hue  = hue % 255;

        CRGB col;
        col.setHSV(hue,255,255);
        color_leds(1, 50, col);
        delay (20);
        //offset_cnt = offset_cnt + 5;
      }
}
