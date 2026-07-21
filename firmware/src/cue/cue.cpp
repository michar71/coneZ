#include <cmath>
#include <new>
#include <stdint.h>
#include <string.h>
#include "cue.h"
#include "main.h"
#include "config.h"
#include "led.h"
#include "gps.h"
#include "printManager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ---------- State ----------

static SemaphoreHandle_t cue_mutex = nullptr;  // protects cue_list/count/cursor/playing

static cue_entry *cue_list = nullptr;

// Per-cue runtime state, parallel to cue_list: the effective start time for
// THIS cone (base start + spatial offset, precomputed at cue_start since the
// offset is fixed during playback) and whether the cue has already fired.
struct cue_rt { int64_t eff_start; uint8_t fired; };
static cue_rt *cue_rt_arr = nullptr;

static int   cue_count   = 0;
static int   cue_fired_count = 0;   // cues fired this run (progress + completion)
static uint64_t music_start_ms = 0; // epoch ms when music started
static bool  playing = false;
static bool  spatial_enabled = false; // false when no GPS fix -> spatial offsets forced to 0

// Precomputed cone position in meter-space
static float my_x = 0, my_y = 0;
static float origin_x = 0, origin_y = 0;

// ---------- Helpers ----------

// Evaluate group targeting: does this cue apply to us?
static bool cue_matches(uint16_t group)
{
    int mode  = group >> 12;
    int value = group & 0x0FFF;
    switch (mode) {
        case 0: return true;                                    // all
        case 1: return config.cone_id == value;                 // cone_id
        case 2: return config.cone_group == value;              // group_id
        // value is a 12-bit mask; a shift by cone_group >= 12 is UB (and >= 32
        // aliases wrong groups on Xtensa). Groups >= 12 simply aren't in the mask.
        case 3: return config.cone_group < 12 && ((value >> config.cone_group) & 1);   // group_mask
        case 4: return config.cone_id != value;                 // not_cone_id
        case 5: return config.cone_group != value;              // not_group_id
        case 6: return !(config.cone_group < 12 && ((value >> config.cone_group) & 1)); // not_mask
        default: return false;
    }
}


// Compute per-cone spatial time offset in milliseconds.
static int32_t compute_spatial_offset(const cue_entry *cue)
{
    if (!spatial_enabled) return 0;   // no GPS fix -> no per-cone spatial timing
    if (cue->spatial_mode == SPATIAL_NONE) return 0;

    float ox, oy;   // effective origin in meter-space
    switch (cue->spatial_mode) {
        case SPATIAL_RADIAL_CONFIG:
        case SPATIAL_DIR_CONFIG:
            ox = origin_x;
            oy = origin_y;
            break;
        case SPATIAL_RADIAL_ABSOLUTE:
        case SPATIAL_DIR_ABSOLUTE:
            latlon_to_meters(cue->spatial_param1, cue->spatial_param2, &ox, &oy);
            break;
        case SPATIAL_RADIAL_RELATIVE:
        case SPATIAL_DIR_RELATIVE:
            ox = origin_x + cue->spatial_param2;  // east_m
            oy = origin_y + cue->spatial_param1;  // north_m
            break;
        default:
            return 0;
    }

    float dist;
    if (cue->spatial_mode <= SPATIAL_RADIAL_RELATIVE) {
        // Radial: distance from effective origin
        GeoResult geo = xy_to_polar(ox, oy, my_x, my_y);
        dist = geo.distance;
    } else {
        // Directional: signed projection along bearing
        float dx = my_x - ox;
        float dy = my_y - oy;
        float angle_rad = cue->spatial_angle * (M_PI / 180.0f);
        dist = dx * sinf(angle_rad) + dy * cosf(angle_rad);
    }

    float result = dist * cue->spatial_delay;
    if (result > 2147483647.0f) return 2147483647;
    if (result < -2147483648.0f) return -2147483647;
    return (int32_t)result;
}


// Dispatch a cue action (fill, stop, blackout, etc.)
static void dispatch_cue(const cue_entry *cue)
{
    switch (cue->cue_type) {

    case CUE_TYPE_STOP:
        if (cue->channel >= 1 && cue->channel <= 4) {
            int cnt = 0;
            switch (cue->channel) {
                case 1: cnt = config.led_count1; break;
                case 2: cnt = config.led_count2; break;
                case 3: cnt = config.led_count3; break;
                case 4: cnt = config.led_count4; break;
            }
            led_set_channel(cue->channel, cnt, CRGB::Black);
            led_show();
        }
        break;

    case CUE_TYPE_FILL: {
        CRGB col(cue->params[0], cue->params[1], cue->params[2]);
        if (cue->channel >= 1 && cue->channel <= 4) {
            int cnt = 0;
            switch (cue->channel) {
                case 1: cnt = config.led_count1; break;
                case 2: cnt = config.led_count2; break;
                case 3: cnt = config.led_count3; break;
                case 4: cnt = config.led_count4; break;
            }
            led_set_channel(cue->channel, cnt, col);
            led_show();
        }
        break;
    }

    case CUE_TYPE_BLACKOUT:
        led_set_channel(1, config.led_count1, CRGB::Black);
        led_set_channel(2, config.led_count2, CRGB::Black);
        led_set_channel(3, config.led_count3, CRGB::Black);
        led_set_channel(4, config.led_count4, CRGB::Black);
        led_show();
        break;

    case CUE_TYPE_EFFECT:
        printfnl(SOURCE_SYSTEM, "cue: effect dispatch not yet implemented (%s)\n", cue->effect_file);
        break;

    case CUE_TYPE_GLOBAL:
        printfnl(SOURCE_SYSTEM, "cue: global cue type not yet implemented\n");
        break;

    default:
        printfnl(SOURCE_SYSTEM, "cue: unknown cue type %d\n", cue->cue_type);
        break;
    }
}

// ---------- Public API ----------

void cue_setup(void)
{
    if (!cue_mutex) cue_mutex = xSemaphoreCreateMutex();
    cue_list   = nullptr;
    cue_rt_arr = nullptr;
    cue_count  = 0;
    cue_fired_count = 0;
    playing    = false;
}


void cue_loop(void)
{
    if (!playing) return;
    if (xSemaphoreTake(cue_mutex, 0) != pdTRUE) return;  // non-blocking

    if (!playing || !cue_list || !cue_rt_arr) {
        xSemaphoreGive(cue_mutex);
        return;
    }

    uint64_t now_ms = get_epoch_ms();
    if (now_ms == 0) { xSemaphoreGive(cue_mutex); return; }

    // Simple subtraction — epoch time never wraps at midnight.
    // 64-bit elapsed avoids the ~49-day wrap of uint32_t.
    uint64_t elapsed_ms = (now_ms > music_start_ms) ? (now_ms - music_start_ms) : 0;

    // Fire every cue whose (precomputed) effective start has arrived and that
    // hasn't fired yet. A per-cue fired flag rather than a single monotonic
    // cursor is required: spatial offsets make effective start times
    // non-monotonic, so one cue with a large offset must not block later cues.
    for (int i = 0; i < cue_count; i++) {
        if (cue_rt_arr[i].fired) continue;
        if ((uint64_t)cue_rt_arr[i].eff_start > elapsed_ms) continue;  // not yet — keep scanning

        if (cue_matches(cue_list[i].group))
            dispatch_cue(&cue_list[i]);
        cue_rt_arr[i].fired = 1;
        cue_fired_count++;
    }

    // Once every cue has fired, stop playback
    if (cue_fired_count >= cue_count) {
        playing = false;
        printfnl(SOURCE_SYSTEM, "cue: playback complete (%d cues)\n", cue_count);
    }

    xSemaphoreGive(cue_mutex);
}


bool cue_load(const char *path)
{
    if (!littlefs_mounted) {
        printfnl(SOURCE_SYSTEM, "cue: LittleFS not mounted\n");
        return false;
    }

    char fpath[128];
    lfs_path(fpath, sizeof(fpath), path);
    FILE *f = fopen(fpath, "rb");
    if (!f) {
        printfnl(SOURCE_SYSTEM, "cue: cannot open %s\n", path);
        return false;
    }

    // Read header
    cue_header hdr;
    if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        printfnl(SOURCE_SYSTEM, "cue: header read failed\n");
        fclose(f);
        return false;
    }

    // Validate header
    if (hdr.magic != CUE_MAGIC) {
        printfnl(SOURCE_SYSTEM, "cue: bad magic 0x%08X (expected 0x%08X)\n", hdr.magic, CUE_MAGIC);
        fclose(f);
        return false;
    }

    if (hdr.version != 0) {
        printfnl(SOURCE_SYSTEM, "cue: unsupported version %d\n", hdr.version);
        fclose(f);
        return false;
    }

    if (hdr.record_size < sizeof(cue_entry)) {
        printfnl(SOURCE_SYSTEM, "cue: record_size %d too small (need %d)\n", hdr.record_size, (int)sizeof(cue_entry));
        fclose(f);
        return false;
    }

    if (hdr.num_cues == 0) {
        printfnl(SOURCE_SYSTEM, "cue: file has 0 cues\n");
        fclose(f);
        return false;
    }

    // Allocate cue array + parallel runtime-state array
    cue_entry *new_list = new (std::nothrow) cue_entry[hdr.num_cues];
    if (!new_list) {
        printfnl(SOURCE_SYSTEM, "cue: alloc failed for %d cues\n", hdr.num_cues);
        fclose(f);
        return false;
    }
    cue_rt *new_rt = new (std::nothrow) cue_rt[hdr.num_cues]();  // zero-init (fired=0)
    if (!new_rt) {
        printfnl(SOURCE_SYSTEM, "cue: alloc failed for %d cues\n", hdr.num_cues);
        delete[] new_list;
        fclose(f);
        return false;
    }

    // Read entries, using record_size to stride for forward compatibility
    uint16_t skip = hdr.record_size - sizeof(cue_entry);
    for (int i = 0; i < hdr.num_cues; i++) {
        if (fread(&new_list[i], 1, sizeof(cue_entry), f) != sizeof(cue_entry)) {
            printfnl(SOURCE_SYSTEM, "cue: read failed at entry %d\n", i);
            delete[] new_list;
            delete[] new_rt;
            fclose(f);
            return false;
        }
        // Skip extra bytes if newer format has larger records
        if (skip > 0) {
            fseek(f, skip, SEEK_CUR);
        }
    }

    fclose(f);

    // Replace previous cue list + runtime state under mutex (cue_loop may read)
    xSemaphoreTake(cue_mutex, portMAX_DELAY);
    cue_entry *old_list = cue_list;
    cue_rt    *old_rt   = cue_rt_arr;
    cue_list   = new_list;
    cue_rt_arr = new_rt;
    cue_count  = hdr.num_cues;
    cue_fired_count = 0;
    playing    = false;
    xSemaphoreGive(cue_mutex);

    delete[] old_list;
    delete[] old_rt;

    printfnl(SOURCE_SYSTEM, "cue: loaded %d cues from %s\n", cue_count, path);
    return true;
}


void cue_start(uint64_t epoch_start_ms)
{
    xSemaphoreTake(cue_mutex, portMAX_DELAY);
    if (!cue_list || !cue_rt_arr || cue_count == 0) {
        xSemaphoreGive(cue_mutex);
        printfnl(SOURCE_SYSTEM, "cue: no cue file loaded\n");
        return;
    }

    music_start_ms = epoch_start_ms;
    cue_fired_count = 0;

    // Precompute origin, then the cone position. Only enable per-cone spatial
    // timing with a real GPS fix: without one, get_lat/lon read 0,0 and the
    // continent-scale distance to origin would push every spatial cue far into
    // the future (cone stays dark). Fall back to base start times (offset 0).
    latlon_to_meters(config.origin_lat, config.origin_lon, &origin_x, &origin_y);
    spatial_enabled = get_gpsstatus();
    if (spatial_enabled) {
        latlon_to_meters(get_lat(), get_lon(), &my_x, &my_y);
    } else {
        my_x = origin_x;
        my_y = origin_y;
        printfnl(SOURCE_SYSTEM, "cue: no GPS fix — spatial timing disabled (base times only)\n");
    }

    // Precompute each cue's effective start for this cone (the offset is fixed
    // for the run) and clear the fired flags.
    for (int i = 0; i < cue_count; i++) {
        int64_t eff = (int64_t)cue_list[i].start_ms + compute_spatial_offset(&cue_list[i]);
        if (eff < 0) eff = 0;
        cue_rt_arr[i].eff_start = eff;
        cue_rt_arr[i].fired = 0;
    }

    playing = true;
    xSemaphoreGive(cue_mutex);

    printfnl(SOURCE_SYSTEM, "cue: playback started (%d cues)\n", cue_count);
}


void cue_stop(void)
{
    xSemaphoreTake(cue_mutex, portMAX_DELAY);
    playing = false;
    xSemaphoreGive(cue_mutex);
    printfnl(SOURCE_SYSTEM, "cue: playback stopped\n");
}


bool cue_is_playing(void)
{
    return playing;
}

uint32_t cue_get_elapsed_ms(void)
{
    if (!playing) return 0;
    uint64_t now_ms = get_epoch_ms();
    if (now_ms == 0 || now_ms <= music_start_ms) return 0;
    return (uint32_t)(now_ms - music_start_ms);
}


// ---------- CLI ----------

int cmd_cue(int argc, char **argv)
{
    // No args or "status" — show status
    if (argc < 2 || !strcasecmp(argv[1], "status")) {
        printfnl(SOURCE_COMMANDS, "Cue Engine:\n");
        printfnl(SOURCE_COMMANDS, "  Loaded:  %s\n", cue_list ? "yes" : "no");
        printfnl(SOURCE_COMMANDS, "  Cues:    %d\n", cue_count);
        printfnl(SOURCE_COMMANDS, "  Playing: %s\n", playing ? "yes" : "no");
        if (playing) {
            uint64_t now_ms = get_epoch_ms();
            uint32_t elapsed = (now_ms > music_start_ms) ? (uint32_t)(now_ms - music_start_ms) : 0;
            printfnl(SOURCE_COMMANDS, "  Elapsed: %lu ms\n", (unsigned long)elapsed);
            printfnl(SOURCE_COMMANDS, "  Fired:   %d / %d\n", cue_fired_count, cue_count);
        }
        return 0;
    }

    // cue load <path>
    if (!strcasecmp(argv[1], "load")) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, "Usage: cue load <path>\n");
            return 1;
        }
        return cue_load(argv[2]) ? 0 : 1;
    }

    // cue start [offset_ms]
    // No arg = start now. With arg = started N ms ago (offset semantic).
    if (!strcasecmp(argv[1], "start")) {
        uint64_t now = get_epoch_ms();
        if (now == 0) {
            printfnl(SOURCE_COMMANDS, "cue: no time source available\n");
            return 1;
        }
        uint64_t start_time = now;
        if (argc >= 3) {
            uint32_t offset = strtoul(argv[2], NULL, 0);
            start_time = (now > offset) ? now - offset : 0;
        }
        cue_start(start_time);
        return 0;
    }

    // cue stop
    if (!strcasecmp(argv[1], "stop")) {
        cue_stop();
        return 0;
    }

    printfnl(SOURCE_COMMANDS, "Usage: cue [load <path> | start [ms] | stop | status]\n");
    return 1;
}
