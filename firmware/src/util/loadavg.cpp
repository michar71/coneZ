#include "loadavg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <string.h>

#define LOADAVG_MAX_TASKS   20          // headroom over typical ~12-15 tasks
#define SAMPLE_INTERVAL_US  5000000LL   // 5 seconds

// EWMA decay constants: exp(-5/window), precomputed
static const float DECAY_1  = 0.920044f;   // 1-min window
static const float DECAY_5  = 0.983471f;   // 5-min window
static const float DECAY_15 = 0.994459f;   // 15-min window

// Two snapshot buffers — statically allocated to avoid heap fragmentation
static TaskStatus_t snap_a[LOADAVG_MAX_TASKS];
static TaskStatus_t snap_b[LOADAVG_MAX_TASKS];
static TaskStatus_t *prev_snap = snap_a;
static TaskStatus_t *curr_snap = snap_b;
static UBaseType_t   prev_count = 0;
static uint32_t      prev_total = 0;
static bool          have_baseline = false;

// EWMA accumulators — written by loopTask, read by ShellTask.
// 32-bit float loads/stores are atomic on Xtensa; volatile ensures visibility.
static volatile float la_1  = 0.0f;
static volatile float la_5  = 0.0f;
static volatile float la_15 = 0.0f;
static volatile bool  la_valid = false;

void loadavg_sample(void)
{
    static int64_t last_sample = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_sample < SAMPLE_INTERVAL_US) return;
    last_sample = now;

    uint32_t curr_total = 0;
    UBaseType_t curr_count = uxTaskGetSystemState(
        curr_snap, LOADAVG_MAX_TASKS, &curr_total);

    if (!have_baseline) {
        have_baseline = true;
        TaskStatus_t *tmp = prev_snap;
        prev_snap = curr_snap;
        curr_snap = tmp;
        prev_count = curr_count;
        prev_total = curr_total;
        return;
    }

    // Wall-clock delta normalized for dual-core
    uint64_t delta_total = (uint64_t)(curr_total - prev_total) * portNUM_PROCESSORS;
    if (delta_total == 0) goto swap;

    {
        // Sum idle task counter deltas (IDLE0 + IDLE1)
        uint32_t idle_delta = 0;
        for (UBaseType_t i = 0; i < curr_count; i++) {
            if (strncmp(curr_snap[i].pcTaskName, "IDLE", 4) != 0) continue;
            for (UBaseType_t j = 0; j < prev_count; j++) {
                if (prev_snap[j].xHandle == curr_snap[i].xHandle) {
                    idle_delta += curr_snap[i].ulRunTimeCounter
                                - prev_snap[j].ulRunTimeCounter;
                    break;
                }
            }
        }

        float busy_frac = 1.0f - (float)idle_delta / (float)delta_total;
        if (busy_frac < 0.0f) busy_frac = 0.0f;
        if (busy_frac > 1.0f) busy_frac = 1.0f;

        float sample = busy_frac * portNUM_PROCESSORS;

        if (!la_valid) {
            // Seed with first sample
            la_1  = sample;
            la_5  = sample;
            la_15 = sample;
            la_valid = true;
        } else {
            la_1  = la_1  * DECAY_1  + sample * (1.0f - DECAY_1);
            la_5  = la_5  * DECAY_5  + sample * (1.0f - DECAY_5);
            la_15 = la_15 * DECAY_15 + sample * (1.0f - DECAY_15);
        }
    }

swap:
    TaskStatus_t *tmp = prev_snap;
    prev_snap = curr_snap;
    curr_snap = tmp;
    prev_count = curr_count;
    prev_total = curr_total;
}

float loadavg_1(void)     { return la_1; }
float loadavg_5(void)     { return la_5; }
float loadavg_15(void)    { return la_15; }
bool  loadavg_valid(void) { return la_valid; }
