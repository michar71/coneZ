#ifndef FWHOST_GPS_H
#define FWHOST_GPS_H
// No GPS on the host: WAITFOR PPS returns -1 at once.
static inline bool get_gpsstatus(void) { return false; }
static inline bool get_pps_flag(void) { return false; }
#endif
