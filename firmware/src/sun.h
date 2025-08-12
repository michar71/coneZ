#ifndef _conez_sun_h
#define _conez_sun_h

#include "Arduino.h"

bool sunSetPosition(float latitude, float longitude, int dstOffset);
bool sunSetTZOffset(int dstOffset);
bool sunSetCurrentDate(int year, int month, int day);
bool sunUpdate();
bool validateSunData();
bool sunUpdateViaGPS();
bool sunDataIsValid();
bool sunLight(int mam);
int sunSet();  //Retruns minutes past midnight
int sunRise(); //Returns minutes past midnight


#endif
