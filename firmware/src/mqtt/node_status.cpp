#include "node_status.h"

#include <limits.h>
#include <stdio.h>

static const char *temp_or_null(int16_t centi, char *buf, size_t bufsz)
{
    if (centi == INT16_MIN) {
        return "null";
    }
    snprintf(buf, bufsz, "%.2f", centi / 100.0f);
    return buf;
}

int mqtt_node_status_format_json(const node_status *status, char *buf, size_t bufsz)
{
    if (!status || !buf || bufsz == 0) {
        return -1;
    }

    char board_temp[16];
    char cpu_temp[16];
    const char *board_temp_str = temp_or_null(status->b_temp, board_temp, sizeof(board_temp));
    const char *cpu_temp_str = temp_or_null(status->c_temp, cpu_temp, sizeof(cpu_temp));

    int n = snprintf(
        buf, bufsz,
        "{"
        "\"status\":%u,"
        "\"wifi_rssi\":%d,"
        "\"lora_rssi\":%d,"
        "\"lora_snr\":%d,"
        "\"satellites\":%u,"
        "\"battery_v\":%.1f,"
        "\"solar_v\":%.1f,"
        "\"uptime_s\":%u,"
        "\"heap_b\":%u,"
        "\"board_temp_c\":%s,"
        "\"cpu_temp_c\":%s,"
        "\"alt_m\":%d,"
        "\"lat\":%.6f,"
        "\"longitude\":%.6f,"
        "\"tilt_x_deg\":%.2f,"
        "\"tilt_y_deg\":%.2f"
        "}",
        (unsigned)status->status,
        (int)status->w_rssi,
        (int)status->l_rssi,
        (int)status->l_snr,
        (unsigned)status->sat_cat,
        status->v_bat / 10.0f,
        status->v_solar / 10.0f,
        (unsigned)status->uptime,
        (unsigned)status->heap,
        board_temp_str,
        cpu_temp_str,
        (int)status->alt,
        status->lat,
        status->longitude,
        status->tilt_x,
        status->tilt_y
    );

    if (n < 0 || (size_t)n >= bufsz) {
        return -1;
    }
    return n;
}
