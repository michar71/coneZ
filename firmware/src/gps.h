#ifndef _conez_gps_h
#define _conez_gps_h

int gps_setup();
int gps_loop();

float get_lat(void);
float get_lon(void);
int get_sec(void);
float get_alt(void);
float get_speed(void);
float get_dir(void);
bool get_gpsstatus(void);
float get_org_lat(void);
float get_org_lon(void);

int get_day(void);
int get_month(void);
int get_year(void);
int get_day_of_week(void);
int get_dayofyear(void);
bool get_isleapyear(void);
int get_hour(void);
int get_minute(void);
int get_second(void);

bool get_pps(void);
#endif
