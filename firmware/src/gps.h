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
#endif
