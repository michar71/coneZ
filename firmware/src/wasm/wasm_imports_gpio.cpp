#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include <stdint.h>
#include "adc.h"
#include "driver/gpio.h"
#include "util.h"

// --- GPIO ---
//
// A script gets the same pin rules as the `gpio` CLI command: only real GPIOs,
// and never one wired to critical board hardware (USB, PSRAM/LoRa SPI, GPS,
// I2C). gpio_set_direction() on one of those detaches the peripheral until
// reboot -- pin_read(6) used to kill the PSRAM bus. Refused calls are silent
// no-ops (the imports have no error return).

// OK for a script to (re)configure as a plain GPIO.
static bool wasm_pin_writable(int32_t gpio)
{
    return gpio_valid_pin(gpio) && !gpio_is_reserved(gpio);
}

// void pin_set(i32 gpio)
m3ApiRawFunction(m3_pin_set) {
    m3ApiGetArg(int32_t, gpio);
    if (wasm_pin_writable(gpio)) {
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)gpio, 1);
    }
    m3ApiSuccess();
}

// void pin_clear(i32 gpio)
m3ApiRawFunction(m3_pin_clear) {
    m3ApiGetArg(int32_t, gpio);
    if (wasm_pin_writable(gpio)) {
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)gpio, 0);
    }
    m3ApiSuccess();
}

// i32 pin_read(i32 gpio)
m3ApiRawFunction(m3_pin_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, gpio);
    if (!gpio_valid_pin(gpio))
        m3ApiReturn(0);
    // A reserved pin is read as-is, like `gpio read`: switching it to an input
    // is exactly the reconfiguration that breaks its peripheral.
    if (!gpio_is_reserved(gpio))
        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
    m3ApiReturn(gpio_get_level((gpio_num_t)gpio));
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
