#include <math.h>
#include <sunset.h>
#include "sun.h"
#include "gps.h"
#include "config.h"

SunSet sun;
int lastDay = -1;
int lastMonth = -1;
int lastYear = -1;
int DST_Offset = -1;
    
// Marked volatile for cross-core visibility (Core 1 writes, Core 0 reads).
volatile int sunrise;
volatile int sunset;
float lastLat;
float lastLong;

bool dataIsValid = false;


// Day-of-week using Zeller's congruence. Returns 0=Sunday..6=Saturday.
static int day_of_week(int year, int month, int day)
{
    if (month < 3) {
        month += 12;
        year -= 1;
    }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13 * (month + 1)) / 5 + k + k/4 + j/4 + 5*j) % 7;
    return ((h + 6) % 7);  // convert: 0=Sat -> 0=Sun
}


// US DST (since 2007): 2nd Sunday of March to 1st Sunday of November.
bool is_us_dst(int year, int month, int day)
{
    if (month < 3 || month > 11) return false;   // Jan, Feb, Dec
    if (month > 3 && month < 11) return true;    // Apr–Oct

    if (month == 3) {
        // 2nd Sunday: find day-of-week of March 1, then compute date
        int dow_mar1 = day_of_week(year, 3, 1);  // 0=Sun
        int second_sun = (dow_mar1 == 0) ? 8 : (15 - dow_mar1);
        return day >= second_sun;
    }

    // month == 11
    int dow_nov1 = day_of_week(year, 11, 1);     // 0=Sun
    int first_sun = (dow_nov1 == 0) ? 1 : (8 - dow_nov1);
    return day < first_sun;
}


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

    // Compute effective timezone offset (auto-DST adds +1 during US DST)
    int tz = config.timezone;
    if (config.auto_dst && is_us_dst(lastYear, lastMonth, lastDay))
        tz += 1;
    DST_Offset = tz;

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

// Simplified solar position from UTC time and lat/lon.
// Returns azimuth (0=N, 90=E) and elevation in degrees.
static bool calc_sun_position(float *az, float *el)
{
    if (!dataIsValid || !get_time_valid()) return false;

    float lat = lastLat;
    float lon = lastLong;
    int yr = get_year(), mo = get_month(), dy = get_day();
    int hr = get_hour(), mn = get_minute(), sc = get_second();

    // Julian day number
    int a = (14 - mo) / 12;
    int y = yr + 4800 - a;
    int m = mo + 12 * a - 3;
    double jdn = dy + (153 * m + 2) / 5 + 365 * y + y/4 - y/100 + y/400 - 32045;
    double jd = jdn + (hr - 12) / 24.0 + mn / 1440.0 + sc / 86400.0;

    // Days since J2000.0
    double n = jd - 2451545.0;

    // Mean longitude and mean anomaly (degrees)
    double L = fmod(280.460 + 0.9856474 * n, 360.0);
    if (L < 0) L += 360.0;
    double g = fmod(357.528 + 0.9856003 * n, 360.0);
    if (g < 0) g += 360.0;
    double g_rad = g * M_PI / 180.0;

    // Ecliptic longitude
    double lambda = L + 1.915 * sin(g_rad) + 0.020 * sin(2.0 * g_rad);
    double lambda_rad = lambda * M_PI / 180.0;

    // Obliquity of ecliptic
    double eps = 23.439 - 0.0000004 * n;
    double eps_rad = eps * M_PI / 180.0;

    // Solar declination
    double sin_dec = sin(eps_rad) * sin(lambda_rad);
    double dec = asin(sin_dec);
    double cos_dec = cos(dec);

    // Right ascension
    double ra = atan2(cos(eps_rad) * sin(lambda_rad), cos(lambda_rad));

    // Greenwich mean sidereal time (hours -> radians)
    double gmst_hr = fmod(6.697375 + 0.0657098242 * n + (double)hr + (double)mn / 60.0 + (double)sc / 3600.0, 24.0);
    if (gmst_hr < 0) gmst_hr += 24.0;
    double gmst_rad = gmst_hr * M_PI / 12.0;

    // Hour angle
    double ha = gmst_rad + lon * M_PI / 180.0 - ra;

    double lat_rad = lat * M_PI / 180.0;

    // Elevation
    double sin_el = sin(lat_rad) * sin_dec + cos(lat_rad) * cos_dec * cos(ha);
    *el = (float)(asin(sin_el) * 180.0 / M_PI);

    // Azimuth (measured from North, clockwise)
    double az_rad = atan2(-sin(ha), cos(lat_rad) * tan(dec) - sin(lat_rad) * cos(ha));
    double az_deg = az_rad * 180.0 / M_PI;
    if (az_deg < 0) az_deg += 360.0;
    *az = (float)az_deg;

    return true;
}

float sunAzimuth()
{
    float az, el;
    if (!calc_sun_position(&az, &el)) return -1000.0f;
    return az;
}

float sunElevation()
{
    float az, el;
    if (!calc_sun_position(&az, &el)) return -1000.0f;
    return el;
}
