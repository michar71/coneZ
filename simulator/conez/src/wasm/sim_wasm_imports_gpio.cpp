#include "sim_wasm_imports.h"
#include "sim_wasm_runtime.h"
#include "m3_env.h"

#include <cstdio>

// GPIO stubs: log to console

m3ApiRawFunction(m3_pin_set) {
    m3ApiGetArg(int32_t, gpio);
    auto *rt = currentRuntime();
    if (rt) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[GPIO] pin_set(%d)\n", gpio);
        rt->emitOutput(buf);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_pin_clear) {
    m3ApiGetArg(int32_t, gpio);
    auto *rt = currentRuntime();
    if (rt) {
        char buf[64];
        snprintf(buf, sizeof(buf), "[GPIO] pin_clear(%d)\n", gpio);
        rt->emitOutput(buf);
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_pin_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, gpio);
    (void)gpio;
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_analog_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, pin);
    (void)pin;
    m3ApiReturn(0);
}

#define LINK(name, sig, fn) \
    r = m3_LinkRawFunction(module, "env", name, sig, fn); \
    if (r && r != m3Err_functionLookupFailed) return r;

M3Result link_gpio_imports(IM3Module module)
{
    M3Result r;
    LINK("pin_set",     "v(i)", m3_pin_set)
    LINK("pin_clear",   "v(i)", m3_pin_clear)
    LINK("pin_read",    "i(i)", m3_pin_read)
    LINK("analog_read", "i(i)", m3_analog_read)
    return m3Err_none;
}

#undef LINK
