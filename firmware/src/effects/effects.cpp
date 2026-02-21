#include <stdint.h>
#include "compat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "main.h"
#include "led.h"
#include "util.h"
#include "gps.h"
#include "printManager.h"


extern volatile bool gps_pos_valid;



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

    //Calculate Offset From Equator/0-meridian in Meters
    float lat_m;
    float lon_m;
    float lat_o_m;
    float lon_o_m;
    float dist_meters;

    //Calculate distance from Origin
    latlon_to_meters(lat, lon,&lat_m,&lon_m);
    latlon_to_meters(get_org_lat(), get_org_lon(),&lat_o_m,&lon_o_m);
    GeoResult res = xy_to_polar(lat_m,lon_m, lat_o_m,lon_o_m);
    dist_meters = res.distance;

    printfnl(SOURCE_OTHER, "Dist: %.2f", dist_meters);

    //Calculate offset in MS with speed of sound being 343m/s
    float sos_ms = 343.0;

    float offset_ms = dist_meters / sos_ms * 1000;
    printfnl(SOURCE_OTHER, "Offset %.2f", offset_ms);
    printfnl(SOURCE_OTHER, "sec = %d", sec);

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec && sec%10 == 0)
    {
        prev_sec = sec;

        //Wait Offset MS
        vTaskDelay(pdMS_TO_TICKS((int)round(offset_ms)));
        printfnl(SOURCE_OTHER, "PING - sec = %d", sec);

        //Flash Light
        for (int ii = 255; ii>=0; ii=ii-32)
        {
          CRGB col;
          col.r = ii;
          col.g = ii;
          col.b = ii;
          led_set_channel(1, 50, col);
          led_show();
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        vTaskDelay(pdMS_TO_TICKS(25));
        led_set_channel(1, 50, CRGB::Black);
        led_show();
      }
}



void SOS_effect2(void)
{
    enum State { IDLE, WAIT_OFFSET, RAMP_UP, RAMP_DOWN };

    static State state = IDLE;
    static int prev_sec = 0;
    static unsigned long target_ms = 0;
    static int step = 0;
    static float offset_ms = 0;

    int ms_per_cycle = 3000;
    float sos_speed_scaling = 0.5;

    switch (state) {

    case IDLE: {
        int sec = get_sec();

        if (sec != prev_sec && sec % 3 == 0)
        {
            prev_sec = sec;

            //Get Lat/Lon/Time
            float lat = get_lat();
            float lon = get_lon();

            //Calculate distance from Origin
            float lat_m, lon_m, lat_o_m, lon_o_m;
            latlon_to_meters(lat, lon, &lat_m, &lon_m);
            latlon_to_meters(get_org_lat(), get_org_lon(), &lat_o_m, &lon_o_m);
            GeoResult res = xy_to_polar(lat_m, lon_m, lat_o_m, lon_o_m);
            float dist_meters = res.distance;

            printfnl(SOURCE_OTHER, "Dist: %.2f", dist_meters);

            // Calculate offset in ms with speed of sound being approximately 343m/s
            const float sos_mps = 343.0 * sos_speed_scaling;
            offset_ms = dist_meters / sos_mps * 1000;
            offset_ms = fmod(offset_ms, ms_per_cycle);

            printfnl(SOURCE_OTHER, "Offset %.2f", offset_ms);
            printfnl(SOURCE_OTHER, "sec = %d", sec);

            target_ms = uptime_ms() + (unsigned long)round(offset_ms);
            state = WAIT_OFFSET;
        }
        break;
    }

    case WAIT_OFFSET:
        if (uptime_ms() >= target_ms)
        {
            printfnl(SOURCE_OTHER, "PING - offset_ms = %.2f", offset_ms);

            step = 0;
            target_ms = uptime_ms();
            state = RAMP_UP;
        }
        break;

    case RAMP_UP: {
        if (uptime_ms() < target_ms)
            break;

        // step 0..15 → brightness 0,16,32,...,240
        int brightness = step * 16;
        CRGB col;
        col.r = brightness;
        col.g = brightness;
        col.b = brightness;
        led_set_channel(1, 50, col);
        led_show();

        step++;
        target_ms = uptime_ms() + 20;

        if (step >= 16)
        {
            step = 0;
            state = RAMP_DOWN;
        }
        break;
    }

    case RAMP_DOWN: {
        if (uptime_ms() < target_ms)
            break;

        // step 0..31 → brightness 255,247,239,...,7 (255 - step*8)
        int brightness = 255 - step * 8;
        if (brightness < 0) brightness = 0;
        CRGB col;
        col.r = brightness;
        col.g = brightness;
        col.b = brightness;
        led_set_channel(1, 50, col);
        led_show();

        step++;
        target_ms = uptime_ms() + 20;

        if (step >= 32)
        {
            // Baseline green glow (or blue if not valid GPS)
            CRGB baseline;
            if (gps_pos_valid)
            {
                baseline.r = 0;
                baseline.g = 4;
                baseline.b = 0;
            }
            else
            {
                baseline.r = 0;
                baseline.g = 0;
                baseline.b = 10;
            }
            led_set_channel(1, 50, baseline);
            led_show();

            state = IDLE;
        }
        break;
    }

    } // switch
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

    //Calculate Offset From Equator/0-meridian in Meters
    float lat_m;
    float lon_m;
    float lat_o_m;
    float lon_o_m;
    float deg;

    //Calculate distance from Origin
    latlon_to_meters(lat, lon,&lat_m,&lon_m);
    latlon_to_meters(get_org_lat(), get_org_lon(),&lat_o_m,&lon_o_m);
    GeoResult res = xy_to_polar(lat_m,lon_m, lat_o_m,lon_o_m);
    deg = res.bearing_deg;
    int offset_ms = (int)(deg * 10);

    printfnl(SOURCE_OTHER, "Deg: %.2f", deg);

    //Wait for sec to roll over Mod 10;
    if (sec != prev_sec /*&& sec%2 == 0*/ )
    {
        prev_sec = sec;

        //Wait Offset MS
        vTaskDelay(pdMS_TO_TICKS((int)round(offset_ms)));
        printfnl(SOURCE_OTHER, "PING - sec = %d", sec);
      
        int hue = (int)map(deg,0,360,0,255);
        //hue = hue + offset_cnt;
        hue = hue + map( get_sec(), 0, 59, 0, 255 );
        hue  = hue % 256;

        CRGB col;
        col.setHSV(hue,255,255);
        led_set_channel(1, 50, col);
        led_show();
        vTaskDelay(pdMS_TO_TICKS(20));
        //offset_cnt = offset_cnt + 5;
      }
}
