#ifndef _conez_gps_h
#define _conez_gps_h

int gps_setup();
int gps_loop();
void gps_send_nmea(const char *body);  // send $body*CS\r\n with auto-checksum

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

int get_date_raw(void);    // DDMMYY or -1 if invalid
int get_time_raw(void);    // HHMMSS or -1 if invalid

int get_satellites(void);
int get_hdop(void);
int get_fix_type(void);    // 0=unknown, 1=no fix, 2=2D, 3=3D
float get_pdop(void);
float get_vdop(void);

bool get_pps(void);
uint32_t get_pps_age_ms(void);  // ms since last PPS edge (UINT32_MAX if never)
uint32_t get_pps_count(void);  // total PPS edges since boot

// --- Unified time API ---
void     time_seed_compile(void); // seed time from __DATE__/__TIME__ (fallback)
void     pps_isr_init(void);     // attach PPS interrupt (called from gps_setup)
bool     get_time_valid(void);   // true if any time source (GPS+PPS or NTP) is active
uint64_t get_epoch_ms(void);     // ms since Unix epoch, interpolated between updates
uint8_t  get_time_source(void);  // 0=compile/none, 1=NTP, 2=GPS+PPS
uint32_t get_ntp_last_sync_ms(void); // millis() at last NTP sync (0 = never)
bool     get_pps_flag(void);     // rising-edge flag, clear-on-read
void     ntp_setup(void);        // call after WiFi connects
void     ntp_loop(void);         // periodic re-sync check

#endif
