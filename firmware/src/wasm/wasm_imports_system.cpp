#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "printManager.h"
#include "main.h"
#include "basic_wrapper.h"   // get_basic_param / set_basic_param
#include "gps.h"
#include "cue.h"
#include "esp_random.h"

// --- Params (inter-task communication) ---

// i32 get_param(i32 id)
m3ApiRawFunction(m3_get_param)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiReturn(get_basic_param(id));
}

// void set_param(i32 id, i32 val)
m3ApiRawFunction(m3_set_param)
{
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, val);
    set_basic_param((uint8_t)id, val);
    m3ApiSuccess();
}

// i32 should_stop() — check if stop was requested (param 0 == 1 or explicit stop)
m3ApiRawFunction(m3_should_stop)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn((wasm_stop_requested || get_basic_param(0) == 1) ? 1 : 0);
}

// --- Cue engine ---

// i32 cue_playing() — 1 if cue engine is active
m3ApiRawFunction(m3_cue_playing) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(cue_is_playing() ? 1 : 0);
}

// i64 cue_elapsed() — ms since cue playback started, 0 if not playing
m3ApiRawFunction(m3_cue_elapsed) {
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)cue_get_elapsed_ms());
}

// --- Random ---

// i32 random_int(i32 min, i32 max) — hardware RNG random in [min, max]
m3ApiRawFunction(m3_random_int)
{
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, min_val);
    m3ApiGetArg(int32_t, max_val);
    if (min_val >= max_val) m3ApiReturn(min_val);
    m3ApiReturn((int32_t)(min_val + (esp_random() % (max_val - min_val))));
}

// --- Event synchronization ---

// i32 wait_pps(i32 timeout_ms) -> 1=received, 0=timeout, -1=no GPS
m3ApiRawFunction(m3_wait_pps) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, timeout_ms);

    if (!get_gpsstatus()) m3ApiReturn(-1);
    get_pps_flag();  // clear stale flag
    unsigned long t = uptime_ms();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1));
        inc_thread_count(xPortGetCoreID());
        if (get_pps_flag()) m3ApiReturn(1);
        if (wasm_stop_requested || get_basic_param(0) == 1) m3ApiReturn(0);
        if (timeout_ms > 0 && (int32_t)(uptime_ms() - t) > timeout_ms) m3ApiReturn(0);
    }
}

// i32 wait_param(i32 id, i32 condition, i32 value, i32 timeout_ms)
// condition: 0=gt, 1=lt, 2=eq, 3=neq.  Returns 1=matched, 0=timeout.
m3ApiRawFunction(m3_wait_param) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, condition);
    m3ApiGetArg(int32_t, value);
    m3ApiGetArg(int32_t, timeout_ms);

    unsigned long t = uptime_ms();
    while (true) {
        int p = get_basic_param(id);
        bool match = false;
        switch (condition) {
            case 0: match = (p > value);  break;  // gt
            case 1: match = (p < value);  break;  // lt
            case 2: match = (p == value); break;  // eq
            case 3: match = (p != value); break;  // neq
        }
        if (match) m3ApiReturn(1);
        if (wasm_stop_requested || get_basic_param(0) == 1) m3ApiReturn(0);
        if (timeout_ms > 0 && (int32_t)(uptime_ms() - t) > timeout_ms) m3ApiReturn(0);
        vTaskDelay(pdMS_TO_TICKS(1));
        inc_thread_count(xPortGetCoreID());
    }
}


// ---------- Link system imports ----------

M3Result link_system_imports(IM3Module module)
{
    M3Result result;

    // Params
    result = m3_LinkRawFunction(module, "env", "get_param", "i(i)", m3_get_param);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "set_param", "v(ii)", m3_set_param);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "should_stop", "i()", m3_should_stop);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Cue
    result = m3_LinkRawFunction(module, "env", "cue_playing", "i()", m3_cue_playing);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "cue_elapsed", "I()", m3_cue_elapsed);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Random
    result = m3_LinkRawFunction(module, "env", "random_int", "i(ii)", m3_random_int);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Event synchronization
    result = m3_LinkRawFunction(module, "env", "wait_pps", "i(i)", m3_wait_pps);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "wait_param", "i(iiii)", m3_wait_param);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
