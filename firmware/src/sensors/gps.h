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

int get_satellites(void);
int get_hdop(void);

bool get_pps(void);

// --- Unified time API ---
void     pps_isr_init(void);     // attach PPS interrupt (called from gps_setup)
bool     get_time_valid(void);   // true if any time source (GPS+PPS or NTP) is active
uint64_t get_epoch_ms(void);     // ms since Unix epoch, interpolated between updates
uint8_t  get_time_source(void);  // 0=none, 1=NTP, 2=GPS+PPS
bool     get_pps_flag(void);     // rising-edge flag, clear-on-read
void     ntp_setup(void);        // call after WiFi connects
void     ntp_loop(void);         // periodic re-sync check

#endif
