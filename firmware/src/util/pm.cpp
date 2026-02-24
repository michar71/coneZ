/*
 * pm.cpp — Dynamic Frequency Scaling (DFS) for ESP32-S3
 *
 * CPU scales between cpu_min and cpu_max MHz based on PM lock state.
 * Valid frequencies: 80, 160, 240 (PLL-derived; APB stays 80 MHz).
 * Below 80 MHz the PLL shuts down, breaking WiFi and SPI PSRAM.
 */

#include "pm.h"
#include "config.h"
#include "main.h"
#include "printManager.h"
#include "conez_usb.h"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"

static esp_pm_lock_handle_t s_cpu_lock = NULL;
static int s_min_mhz = 240;
static int s_max_mhz = 240;
static int s_lock_count = 0;   // track nested acquire count for display

// Validate frequency: must be 80, 160, or 240
static bool valid_freq(int mhz)
{
    return mhz == 80 || mhz == 160 || mhz == 240;
}

void pm_setup(void)
{
    int max_mhz = config.cpu_max;
    int min_mhz = config.cpu_min;

    // Validate and clamp to safe defaults
    if (!valid_freq(max_mhz)) max_mhz = 240;
    if (!valid_freq(min_mhz)) min_mhz = max_mhz;
    if (min_mhz > max_mhz)   min_mhz = max_mhz;

    s_max_mhz = max_mhz;
    s_min_mhz = min_mhz;

    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = max_mhz,
        .min_freq_mhz = min_mhz,
        .light_sleep_enable = false,
    };

    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err != ESP_OK) {
        usb_printf("PM: esp_pm_configure failed (%d)\n", err);
        return;
    }

    err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "conez_cpu", &s_cpu_lock);
    if (err != ESP_OK) {
        usb_printf("PM: lock create failed (%d)\n", err);
        return;
    }

    if (min_mhz != max_mhz)
        usb_printf("DFS: %d-%d MHz\n", min_mhz, max_mhz);
    else
        usb_printf("CPU: %d MHz (fixed)\n", max_mhz);
}

void pm_set_freq(int min_mhz, int max_mhz)
{
    if (!valid_freq(max_mhz) || !valid_freq(min_mhz)) {
        printfnl(SOURCE_COMMANDS, "Error: frequency must be 80, 160, or 240 MHz\n");
        return;
    }
    if (min_mhz > max_mhz) {
        printfnl(SOURCE_COMMANDS, "Error: min (%d) > max (%d)\n", min_mhz, max_mhz);
        return;
    }

    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = max_mhz,
        .min_freq_mhz = min_mhz,
        .light_sleep_enable = false,
    };

    esp_err_t err = esp_pm_configure(&pm_cfg);
    if (err != ESP_OK) {
        printfnl(SOURCE_COMMANDS, "Error: esp_pm_configure failed (%d)\n", err);
        return;
    }

    s_max_mhz = max_mhz;
    s_min_mhz = min_mhz;

    // Update config struct (not saved to flash — use 'config set' for persistence)
    config.cpu_max = max_mhz;
    config.cpu_min = min_mhz;
}

int pm_get_freq(void)
{
    return esp_clk_cpu_freq() / 1000000;
}

bool pm_is_dfs_active(void)
{
    return s_min_mhz != s_max_mhz;
}

void pm_cpu_lock(void)
{
    if (s_cpu_lock)
        esp_pm_lock_acquire(s_cpu_lock);
    s_lock_count++;
}

void pm_cpu_unlock(void)
{
    if (s_cpu_lock)
        esp_pm_lock_release(s_cpu_lock);
    if (s_lock_count > 0) s_lock_count--;
}

// ---------- CLI ----------

int cmd_cpu(int argc, char **argv)
{
    // "cpu" — show status
    if (argc == 1) {
        int cur = pm_get_freq();
        if (pm_is_dfs_active())
            printfnl(SOURCE_COMMANDS, "CPU: %d MHz  (DFS: %d-%d MHz)  lock count: %d\n",
                     cur, s_min_mhz, s_max_mhz, s_lock_count);
        else
            printfnl(SOURCE_COMMANDS, "CPU: %d MHz  (fixed)  lock count: %d\n",
                     cur, s_lock_count);
        return 0;
    }

    const char *sub = argv[1];

    // "cpu auto" — enable DFS (80-240)
    if (strcasecmp(sub, "auto") == 0) {
        int max = (argc >= 4) ? atoi(argv[3]) : s_max_mhz;
        int min = (argc >= 3) ? atoi(argv[2]) : 80;
        pm_set_freq(min, max);
        if (pm_is_dfs_active())
            printfnl(SOURCE_COMMANDS, "DFS enabled: %d-%d MHz\n", s_min_mhz, s_max_mhz);
        else
            printfnl(SOURCE_COMMANDS, "CPU fixed at %d MHz\n", s_max_mhz);
        return 0;
    }

    // "cpu min <N>" / "cpu max <N>"
    if (strcasecmp(sub, "min") == 0 && argc == 3) {
        pm_set_freq(atoi(argv[2]), s_max_mhz);
        return 0;
    }
    if (strcasecmp(sub, "max") == 0 && argc == 3) {
        pm_set_freq(s_min_mhz, atoi(argv[2]));
        return 0;
    }

    // "cpu 80|160|240" — set fixed frequency
    int freq = atoi(sub);
    if (valid_freq(freq)) {
        pm_set_freq(freq, freq);
        printfnl(SOURCE_COMMANDS, "CPU fixed at %d MHz\n", freq);
        return 0;
    }

    printfnl(SOURCE_COMMANDS, "Usage:\n");
    printfnl(SOURCE_COMMANDS, "  cpu                 Show CPU frequency and DFS status\n");
    printfnl(SOURCE_COMMANDS, "  cpu 80|160|240      Set fixed frequency\n");
    printfnl(SOURCE_COMMANDS, "  cpu auto [min max]  Enable DFS (default: 80-%d MHz)\n", s_max_mhz);
    printfnl(SOURCE_COMMANDS, "  cpu min <MHz>       Set minimum frequency\n");
    printfnl(SOURCE_COMMANDS, "  cpu max <MHz>       Set maximum frequency\n");
    return 1;
}
