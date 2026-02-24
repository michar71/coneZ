#ifndef _conez_loadavg_h
#define _conez_loadavg_h

// Background CPU load average computation.
// Call loadavg_sample() from loop() every iteration — it rate-limits
// internally to one sample every 5 seconds. Getters return EWMA load
// averages in [0.0, portNUM_PROCESSORS] where 2.0 = both cores 100% busy.

void  loadavg_sample(void);
float loadavg_1(void);
float loadavg_5(void);
float loadavg_15(void);
bool  loadavg_valid(void);

#endif
