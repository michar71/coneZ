#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "m3_env.h"

#include <chrono>
#include <ctime>
#include <thread>

static auto s_boot_time = std::chrono::steady_clock::now();

m3ApiRawFunction(m3_get_epoch_ms) {
    m3ApiReturnType(int64_t);
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    m3ApiReturn(ms);
}

m3ApiRawFunction(m3_millis) {
    m3ApiReturnType(int32_t);
    auto elapsed = std::chrono::steady_clock::now() - s_boot_time;
    m3ApiReturn((int32_t)std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

m3ApiRawFunction(m3_delay_ms) {
    m3ApiGetArg(int32_t, ms);
    if (ms > 0) {
        // Sleep in small chunks to remain responsive to stop requests
        auto *rt = currentRuntime();
        int remaining = ms;
        while (remaining > 0 && rt && !rt->isStopRequested()) {
            int chunk = remaining > 10 ? 10 : remaining;
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
            remaining -= chunk;
        }
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_time_valid) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(1); // Always valid in simulator
}

m3ApiRawFunction(m3_get_uptime_ms) {
    m3ApiReturnType(int64_t);
    auto elapsed = std::chrono::steady_clock::now() - s_boot_time;
    m3ApiReturn(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

m3ApiRawFunction(m3_get_last_comm_ms) {
    m3ApiReturnType(int64_t);
    m3ApiReturn((int64_t)0);
}

// Calendar fields from system clock
static struct tm get_localtime() {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm t;
    localtime_r(&now, &t);
    return t;
}

m3ApiRawFunction(m3_get_year) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_year + 1900);
}

m3ApiRawFunction(m3_get_month) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_mon + 1);
}

m3ApiRawFunction(m3_get_day) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_mday);
}

m3ApiRawFunction(m3_get_hour) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_hour);
}

m3ApiRawFunction(m3_get_minute) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_min);
}

m3ApiRawFunction(m3_get_second) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_sec);
}

m3ApiRawFunction(m3_get_day_of_week) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_wday);
}

m3ApiRawFunction(m3_get_day_of_year) {
    m3ApiReturnType(int32_t);
    m3ApiReturn(get_localtime().tm_yday + 1);
}

m3ApiRawFunction(m3_get_is_leap_year) {
    m3ApiReturnType(int32_t);
    int y = get_localtime().tm_year + 1900;
    m3ApiReturn((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0) ? 1 : 0);
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_datetime_imports(IM3Module module)
{
    M3Result r;
    LINK("get_epoch_ms",    "I()", m3_get_epoch_ms)
    LINK("millis",          "i()", m3_millis)
    LINK("delay_ms",        "v(i)", m3_delay_ms)
    LINK("time_valid",      "i()", m3_time_valid)
    LINK("get_uptime_ms",   "I()", m3_get_uptime_ms)
    LINK("get_last_comm_ms","I()", m3_get_last_comm_ms)

    LINK("get_year",        "i()", m3_get_year)
    LINK("get_month",       "i()", m3_get_month)
    LINK("get_day",         "i()", m3_get_day)
    LINK("get_hour",        "i()", m3_get_hour)
    LINK("get_minute",      "i()", m3_get_minute)
    LINK("get_second",      "i()", m3_get_second)
    LINK("get_day_of_week", "i()", m3_get_day_of_week)
    LINK("get_day_of_year", "i()", m3_get_day_of_year)
    LINK("get_is_leap_year","i()", m3_get_is_leap_year)

    return m3Err_none;
}

#undef LINK
