#ifndef _conez_pm_h
#define _conez_pm_h

// Dynamic Frequency Scaling (DFS) for ESP32-S3.
// CPU scales between cpu_min and cpu_max MHz based on PM lock state.
// When no locks held → min_freq; ESP_PM_CPU_FREQ_MAX lock → max_freq.
// Valid frequencies: 80, 160, 240 MHz (PLL-derived; APB stays 80 MHz).

void pm_setup(void);                          // Init DFS from config
void pm_set_freq(int min_mhz, int max_mhz);  // Runtime reconfigure
int  pm_get_freq(void);                       // Current CPU freq in MHz
bool pm_is_dfs_active(void);                  // true if min != max
void pm_cpu_lock(void);                       // Acquire CPU_FREQ_MAX lock
void pm_cpu_unlock(void);                     // Release CPU_FREQ_MAX lock
int  cmd_cpu(int argc, char **argv);          // CLI command

#endif
