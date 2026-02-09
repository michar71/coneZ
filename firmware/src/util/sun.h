#ifndef _conez_sun_h
#define _conez_sun_h

#include "Arduino.h"

bool is_us_dst(int year, int month, int day);
bool sunSetPosition(float latitude, float longitude);
bool sunSetTZOffset(int dstOffset);
bool sunSetCurrentDate(int year, int month, int day);
bool sunUpdate();
bool validateSunData();
bool sunUpdateViaGPS();
bool sunDataIsValid();
bool sunLight(int mam);
int sunSet();  //Retruns minutes past midnight
int sunRise(); //Returns minutes past midnight
float sunAzimuth();   // degrees, -1000 if invalid
float sunElevation(); // degrees, -1000 if invalid


#endif
