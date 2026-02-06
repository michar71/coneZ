#ifndef main_h
#define main_h

#include "board.h"
#include "led.h"

#define FSLINK LittleFS

//#define configGENERATE_RUN_TIME_STATS
//#define configUSE_STATS_FORMATTING_FUNCTIONS

// Pin definitions are in board.h

// Structure to hold result
typedef struct {
    float distance;   // Distance in meters
    float bearing_deg;  // Bearing in degrees
} GeoResult;

GeoResult xy_to_polar(float x1, float y1, float x2, float y2);
void latlon_to_meters(float latitude_deg, float longitude_deg,
                      float *x_offset_meters, float *y_offset_meters);


#endif