#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "printManager.h"
#include "main.h"
#include "gps.h"

// --- Time ---

// I64 get_epoch_ms()
m3ApiRawFunction(m3_get_epoch_ms)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)get_epoch_ms());
}

// i32 millis_()  - renamed to avoid collision with Arduino millis()
m3ApiRawFunction(m3_millis)
{
    m3ApiReturnType(int32_t);
    m3ApiReturn((int32_t)millis());
}

// I64 millis64()
m3ApiRawFunction(m3_millis64)
{
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)millis());
}

// void delay_ms(i32 ms) — yields to FreeRTOS
m3ApiRawFunction(m3_delay_ms)
{
    m3ApiGetArg(int32_t, ms);
    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    inc_thread_count(xPortGetCoreID());
    m3ApiSuccess();
}

// --- Date/Time ---

m3ApiRawFunction(m3_get_year) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_year());
}
m3ApiRawFunction(m3_get_month) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_month());
}
m3ApiRawFunction(m3_get_day) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_day());
}
m3ApiRawFunction(m3_get_hour) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_hour());
}
m3ApiRawFunction(m3_get_minute) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_minute());
}
m3ApiRawFunction(m3_get_second) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_second());
}
m3ApiRawFunction(m3_get_day_of_week) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_day_of_week());
}
m3ApiRawFunction(m3_get_day_of_year) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_dayofyear());
}
m3ApiRawFunction(m3_get_is_leap_year) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_isleapyear() ? 1 : 0);
}
m3ApiRawFunction(m3_time_valid) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_time_valid() ? 1 : 0);
}

// I64 get_uptime_ms()
m3ApiRawFunction(m3_get_uptime_ms) {
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)millis());
}

// I64 get_last_comm_ms()
// FIXME: return 0 (boot time) until we track last LoRa/HTTP comm timestamp
m3ApiRawFunction(m3_get_last_comm_ms) {
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)0);
}


// ---------- Link datetime imports ----------

M3Result link_datetime_imports(IM3Module module)
{
    M3Result result;

    // Time
    result = m3_LinkRawFunction(module, "env", "get_epoch_ms", "I()", m3_get_epoch_ms);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "millis", "i()", m3_millis);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "millis64", "I()", m3_millis64);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "delay_ms", "v(i)", m3_delay_ms);
    if (result && result != m3Err_functionLookupFailed) return result;

    // Date/Time
    result = m3_LinkRawFunction(module, "env", "get_year", "i()", m3_get_year);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_month", "i()", m3_get_month);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_day", "i()", m3_get_day);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_hour", "i()", m3_get_hour);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_minute", "i()", m3_get_minute);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_second", "i()", m3_get_second);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_day_of_week", "i()", m3_get_day_of_week);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_day_of_year", "i()", m3_get_day_of_year);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_is_leap_year", "i()", m3_get_is_leap_year);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "time_valid", "i()", m3_time_valid);
    if (result && result != m3Err_functionLookupFailed) return result;

    result = m3_LinkRawFunction(module, "env", "get_uptime_ms", "I()", m3_get_uptime_ms);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "get_last_comm_ms", "I()", m3_get_last_comm_ms);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
