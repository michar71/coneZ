#ifndef _conez_gps_h
#define _conez_gps_h

int gps_setup();
int gps_loop();

float get_lat(void);
float get_lon(void);
int get_sec(void);

#endif
