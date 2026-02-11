#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "m3_env.h"

#include <random>
#include <chrono>
#include <thread>

static std::mt19937 s_rng(std::random_device{}());

m3ApiRawFunction(m3_get_param) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    auto *rt = currentRuntime();
    m3ApiReturn(rt ? rt->getParam(id) : 0);
}

m3ApiRawFunction(m3_set_param) {
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, val);
    auto *rt = currentRuntime();
    if (rt) rt->setParam(id, val);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_should_stop) {
    m3ApiReturnType(int32_t);
    auto *rt = currentRuntime();
    m3ApiReturn(rt && rt->isStopRequested() ? 1 : 0);
}

m3ApiRawFunction(m3_random_int) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, lo);
    m3ApiGetArg(int32_t, hi);
    if (hi <= lo) { m3ApiReturn(lo); }
    std::uniform_int_distribution<int> dist(lo, hi - 1);
    m3ApiReturn(dist(s_rng));
}

m3ApiRawFunction(m3_wait_pps) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, timeout_ms);
    // Simulate: wait ~1 second or until stop
    auto *rt = currentRuntime();
    int waited = 0;
    int limit = timeout_ms > 0 ? timeout_ms : 2000;
    while (waited < limit) {
        if (rt && rt->isStopRequested()) { m3ApiReturn(0); }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
        if (waited >= 1000) { m3ApiReturn(1); } // Simulate PPS every second
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_wait_param) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, id);
    m3ApiGetArg(int32_t, condition);
    m3ApiGetArg(int32_t, value);
    m3ApiGetArg(int32_t, timeout_ms);

    auto *rt = currentRuntime();
    int waited = 0;
    int limit = timeout_ms > 0 ? timeout_ms : 60000;
    while (waited < limit) {
        if (rt && rt->isStopRequested()) { m3ApiReturn(0); }
        int p = rt ? rt->getParam(id) : 0;
        bool match = false;
        switch (condition) {
            case 0: match = p > value; break;
            case 1: match = p < value; break;
            case 2: match = p == value; break;
            case 3: match = p != value; break;
        }
        if (match) { m3ApiReturn(1); }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        waited += 10;
    }
    m3ApiReturn(0);
}

// ---- Link ----

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_system_imports(IM3Module module)
{
    M3Result r;
    LINK("get_param",   "i(i)",    m3_get_param)
    LINK("set_param",   "v(ii)",   m3_set_param)
    LINK("should_stop", "i()",     m3_should_stop)
    LINK("random_int",  "i(ii)",   m3_random_int)
    // cue_playing/cue_elapsed linked from sensor imports
    LINK("wait_pps",    "i(i)",    m3_wait_pps)
    LINK("wait_param",  "i(iiii)", m3_wait_param)
    return m3Err_none;
}

#undef LINK
