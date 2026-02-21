#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include <stdint.h>
#include "adc.h"
#include "driver/gpio.h"

// --- GPIO ---

// void pin_set(i32 gpio)
m3ApiRawFunction(m3_pin_set) {
    m3ApiGetArg(int32_t, gpio);
    if (gpio >= 0 && gpio < GPIO_NUM_MAX) {
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)gpio, 1);
    }
    m3ApiSuccess();
}

// void pin_clear(i32 gpio)
m3ApiRawFunction(m3_pin_clear) {
    m3ApiGetArg(int32_t, gpio);
    if (gpio >= 0 && gpio < GPIO_NUM_MAX) {
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)gpio, 0);
    }
    m3ApiSuccess();
}

// i32 pin_read(i32 gpio)
m3ApiRawFunction(m3_pin_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, gpio);
    if (gpio >= 0 && gpio < GPIO_NUM_MAX) {
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
        m3ApiReturn(gpio_get_level((gpio_num_t)gpio));
    }
    m3ApiReturn(0);
}

// i32 analog_read(i32 pin)
m3ApiRawFunction(m3_analog_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, pin);
    m3ApiReturn(adc_read_raw(pin));
}


// ---------- Link GPIO imports ----------

M3Result link_gpio_imports(IM3Module module)
{
    M3Result result;

    result = m3_LinkRawFunction(module, "env", "pin_set", "v(i)", m3_pin_set);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "pin_clear", "v(i)", m3_pin_clear);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "pin_read", "i(i)", m3_pin_read);
    if (result && result != m3Err_functionLookupFailed) return result;
    result = m3_LinkRawFunction(module, "env", "analog_read", "i(i)", m3_analog_read);
    if (result && result != m3Err_functionLookupFailed) return result;

    return m3Err_none;
}

#endif // INCLUDE_WASM
