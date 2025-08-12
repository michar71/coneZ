#include <sunset.h>
#include "sun.h"
#include "gps.h"

SunSet sun;
int lastDay = -1;
int lastMonth = -1;
int lastYear = -1;
int DST_Offset = -1;
    
int sunrise;
int sunset;
float lastLat;
float lastLong;

bool dataIsValid = false;


bool sunSetPosition(float latitude, float longitude)
{
    if (latitude < -90.0 || latitude > 90.0 || longitude < -180.0 || longitude > 180.0) {
        return false;
    }
    lastLat = latitude;
    lastLong = longitude;
    sun.setPosition(latitude, longitude, DST_Offset);
    return true;
}

bool sunSetTZOffset(int dstOffset)
{
    if (dstOffset < -12 || dstOffset > 14) {
        return false;
    }
    DST_Offset = dstOffset;
    sun.setTZOffset(dstOffset);
    return true;
}

bool sunSetCurrentDate(int year, int month, int day)
{
    if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }
    lastDay = day;
    lastMonth = month;
    lastYear = year;
    return sun.setCurrentDate(year, month, day);
}


bool sunUpdate()
{
    sunrise = static_cast<int>(sun.calcSunrise());
    sunset = static_cast<int>(sun.calcSunset());
    return true;
}

bool sunUpdateViaGPS()
{
    dataIsValid = false;
    //Get Data from GPS module
    if (!get_gpsstatus()) {
        return false;   
    }
    lastLat = get_lat();
    lastLong = get_lon();
    lastYear = get_year();
    lastMonth = get_month();
    lastDay = get_day();

    if (!validateSunData()) {
        return false; // Invalid GPS data
    }

    if (!sunSetPosition(lastLat, lastLong)) {
        return false;
    }
    if (!sunSetTZOffset(DST_Offset)) {
        return false;
    }
    if (!sunSetCurrentDate(lastYear, lastMonth, lastDay)) {
        return false;
    }

    dataIsValid =  sunUpdate();
    return dataIsValid;
}

bool sunDataIsValid()
{
    return dataIsValid;
}

bool validateSunData()
{
    if (lastLat < -90.0 || lastLat > 90.0 || lastLong < -180.0 || lastLong > 180.0) {
        return false;
    }
    if (lastYear < 1970 || lastYear > 2100 || lastMonth < 1 || lastMonth > 12 || lastDay < 1 || lastDay > 31) {
        return false;
    }
    if (DST_Offset < -12 || DST_Offset > 14) {
        return false;
    }
    
    return true;
}

bool sunLight(int mam)
{
    
    if (mam < 0 || mam >= 1440) {
        return false; // Invalid time
    }

    if (!sunUpdateViaGPS())
    {
        return false; // Failed to update sun data via GPS
    }
    
    if (sunDataIsValid()) {

        if (sunrise < 0 || sunset < 0) {
            return false; // Invalid sunrise/sunset times
        }
        
        if (mam >= sunrise && mam < sunset) {
            return true; // It's light
        } else {
            return false; // It's dark
        }
    }
    return false; // Data is not valid
}

int sunSet() //Retruns minutes past midnight
{
    if (!sunDataIsValid()) {
        return -1; // Invalid data
    }
    if (sunset < 0) {
        return -1; // Invalid sunset time
    }
    return sunset;
}

int sunRise() //Returns minutes past midnight
{
    if (!sunDataIsValid()) {
        return -1; // Invalid data
    }
    if (sunrise < 0) {
        return -1; // Invalid sunrise time
    }
    return sunrise;
}
