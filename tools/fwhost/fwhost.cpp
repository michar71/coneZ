// fwhost -- run a .wasm module on this machine with the FIRMWARE's own WASM
// host code: the wasm3 fork (firmware/lib/wasm3), the run sequence
// (wasm_wrapper.cpp), the linear-memory glue (wasm_psram_glue.cpp) and the
// string / FORMAT / print / file / math / system imports, compiled unchanged
// from firmware/src/wasm. Only the hardware is replaced: see README.txt and
// shim/.
//
//   fwhost <fs-root> </path/inside/fs-root.wasm>
//
// <fs-root> stands in for the device's LittleFS (a script's OPEN "/x.txt" is
// <fs-root>/x.txt). Everything the script prints goes to stdout, between the
// firmware's own "wasm: running ..." and "wasm: DONE" lines.
//
// Built twice by the Makefile: fwhost-dram (d_m3UsePsramMemory=0, the Heltec
// board: linear memory in ordinary RAM) and fwhost-psram (=1, the ConeZ board:
// first 4 KB in RAM, the rest reached through psram_read()/psram_write()).

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wasm_internal.h"
#include "m3_env.h"
#include "printManager.h"
#include "main.h"
#include "basic_wrapper.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------------------------------------------------------- platform

char fwhost_fs_root[512];

void printfnl(source_e, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stdout, format, ap);
    va_end(ap);
}

// Time only moves when something sleeps, so runs are repeatable.
static unsigned long fake_ms = 0;
unsigned long uptime_ms(void) { return fake_ms; }
void vTaskDelay(TickType_t ticks) { fake_ms += ticks; }

// Same generator as tools/bas2wasm/test/run_runtime.js, so RANDOM() tests have
// one expected output. random_int() does `esp_random() % range`; the Node
// harness uses the state shifted right by 8, hence the shift here.
static uint32_t rng_state = 12345;
uint32_t esp_random(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state >> 8;
}

// Script params, as firmware/src/basic/basic_wrapper.cpp.
#define MAX_PARAMS 16
static int params[MAX_PARAMS];
static bool param_valid(int id) { return id >= 0 && id < MAX_PARAMS; }
bool set_basic_param(int id, int val) { if (!param_valid(id)) return false; params[id] = val; return true; }
int  get_basic_param(int id) { return param_valid(id) ? params[id] : 0; }

// ------------------------------------------------------------------- PSRAM
// Blocks on the host heap behind fake 0x10000000-based addresses. Any access
// outside a live block is a bug in the code under test: report and abort.

#if d_m3UsePsramMemory
#include "psram.h"

#define PS_BASE   0x10000000u
#define PS_SLOT   0x01000000u       // 16 MB of address space per block
#define PS_BLOCKS 64
static struct { uint8_t *p; size_t size; } ps_blk[PS_BLOCKS];

static uint8_t *ps_ptr(uint32_t addr, size_t len, const char *what)
{
    uint32_t i = (addr - PS_BASE) / PS_SLOT;
    uint32_t off = (addr - PS_BASE) % PS_SLOT;
    if (addr < PS_BASE || i >= PS_BLOCKS || !ps_blk[i].p || (uint64_t)off + len > ps_blk[i].size) {
        fflush(stdout);
        fprintf(stderr, "fwhost: %s of %zu bytes at PSRAM 0x%08x is outside any live block\n",
                what, len, (unsigned)addr);
        abort();
    }
    return ps_blk[i].p + off;
}

uint32_t psram_malloc(size_t size)
{
    if (size == 0 || size > PS_SLOT) return 0;
    for (uint32_t i = 0; i < PS_BLOCKS; i++) {
        if (ps_blk[i].p) continue;
        // 0xA5 fill: reading memory nobody initialised shows up as garbage,
        // like the real chip, rather than as convenient zeros.
        ps_blk[i].p = (uint8_t *)malloc(size);
        if (!ps_blk[i].p) return 0;
        memset(ps_blk[i].p, 0xA5, size);
        ps_blk[i].size = size;
        return PS_BASE + i * PS_SLOT;
    }
    return 0;
}

void psram_free(uint32_t addr)
{
    if (!addr) return;
    ps_ptr(addr, 0, "free");
    uint32_t i = (addr - PS_BASE) / PS_SLOT;
    free(ps_blk[i].p);
    ps_blk[i].p = NULL;
    ps_blk[i].size = 0;
}

void psram_read(uint32_t addr, uint8_t *buf, size_t len)        { if (len) memcpy(buf, ps_ptr(addr, len, "read"), len); }
void psram_write(uint32_t addr, const uint8_t *buf, size_t len) { if (len) memcpy(ps_ptr(addr, len, "write"), buf, len); }
void psram_memset(uint32_t addr, uint8_t val, size_t len)       { if (len) memset(ps_ptr(addr, len, "memset"), val, len); }
void psram_memcpy(uint32_t dst, uint32_t src, size_t len)
{
    if (len) memmove(ps_ptr(dst, len, "memcpy to"), ps_ptr(src, len, "memcpy from"), len);
}
#endif

// ------------------------------------------------- hardware import groups
// LED, sensor, date/time, GPIO and compression imports talk to hardware the
// host does not have. They are stubbed with the same fixed answers as the Node
// harness (run_runtime.js), so a test has one expected output on every host.
// Everything else a script can call is the firmware's real code.

m3ApiRawFunction(stub_v) { m3ApiSuccess(); }
m3ApiRawFunction(stub_i) { m3ApiReturnType(int32_t); m3ApiReturn(0); }
m3ApiRawFunction(stub_I) { m3ApiReturnType(int64_t); m3ApiReturn(0); }
m3ApiRawFunction(stub_f) { m3ApiReturnType(float);   m3ApiReturn(0.0f); }

m3ApiRawFunction(stub_one)      { m3ApiReturnType(int32_t); m3ApiReturn(1); }
m3ApiRawFunction(stub_low_byte) { m3ApiReturnType(int32_t); m3ApiGetArg(int32_t, v); m3ApiReturn(v & 0xff); }
m3ApiRawFunction(stub_low_bit)  { m3ApiReturnType(int32_t); m3ApiGetArg(int32_t, v); m3ApiReturn(v & 1); }

struct StubImport { const char *name; const char *sig; M3RawCall fn; };

static M3Result link_stubs(IM3Module module, const StubImport *t)
{
    for (; t->name; t++) {
        M3RawCall fn = t->fn;
        if (!fn) {
            switch (t->sig[0]) {            // pick the generic stub by return type
                case 'v': fn = stub_v; break;
                case 'I': fn = stub_I; break;
                case 'f': fn = stub_f; break;
                default:  fn = stub_i; break;
            }
        }
        M3Result r = m3_LinkRawFunction(module, "env", t->name, t->sig, fn);
        if (r && r != m3Err_functionLookupFailed) return r;
    }
    return m3Err_none;
}

M3Result link_led_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"led_set_pixel", "v(iiiii)"}, {"led_fill", "v(iiii)"}, {"led_show", "v()"},
        {"led_count", "i(i)"}, {"led_set_pixel_hsv", "v(iiiii)"}, {"led_fill_hsv", "v(iiii)"},
        {"hsv_to_rgb", "i(iii)"}, {"rgb_to_hsv", "i(iii)"},
        {"led_gamma8", "i(i)", stub_low_byte},
        {"led_set_gamma", "v(i)"}, {"led_set_buffer", "v(iii)"}, {"led_shift", "v(iiiii)"},
        {"led_rotate", "v(ii)"}, {"led_reverse", "v(i)"}, {}
    };
    return link_stubs(module, t);
}

M3Result link_sensor_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"get_lat", "f()"}, {"get_lon", "f()"}, {"get_alt", "f()"}, {"get_speed", "f()"},
        {"get_dir", "f()"}, {"gps_valid", "i()"}, {"get_roll", "f()"}, {"get_pitch", "f()"},
        {"get_yaw", "f()"}, {"get_acc_x", "f()"}, {"get_acc_y", "f()"}, {"get_acc_z", "f()"},
        {"imu_valid", "i()"}, {"get_temp", "f()"}, {"get_humidity", "f()"},
        {"get_brightness", "f()"}, {"get_bat_voltage", "f()"}, {"get_solar_voltage", "f()"},
        {"get_sunrise", "i()"}, {"get_sunset", "i()"}, {"sun_valid", "i()"},
        {"is_daylight", "i()"}, {"get_origin_lat", "f()"}, {"get_origin_lon", "f()"},
        {"has_origin", "i()"}, {"origin_dist", "f()"}, {"origin_bearing", "f()"},
        {"gps_present", "i()"},
        {"imu_present", "i()", stub_one},       // so a test can see IMUPRESENT() true
        {"get_battery_percentage", "f()"}, {"get_battery_runtime", "f()"},
        {"get_sun_azimuth", "f()"}, {"get_sun_elevation", "f()"}, {}
    };
    return link_stubs(module, t);
}

M3Result link_datetime_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"get_epoch_ms", "I()"}, {"millis", "i()"}, {"millis64", "I()"}, {"delay_ms", "v(i)"},
        {"get_year", "i()"}, {"get_month", "i()"}, {"get_day", "i()"}, {"get_hour", "i()"},
        {"get_minute", "i()"}, {"get_second", "i()"}, {"get_day_of_week", "i()"},
        {"get_day_of_year", "i()"}, {"get_is_leap_year", "i()"}, {"time_valid", "i()"},
        {"get_uptime_ms", "I()"}, {"get_last_comm_ms", "I()"}, {}
    };
    return link_stubs(module, t);
}

M3Result link_gpio_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"pin_set", "v(i)"}, {"pin_clear", "v(i)"},
        {"pin_read", "i(i)", stub_low_bit},     // odd-numbered pins read high
        {"analog_read", "i(i)"}, {}
    };
    return link_stubs(module, t);
}

M3Result link_compression_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"inflate_file", "i(ii)"}, {"inflate_file_to_mem", "i(iii)"}, {"inflate_mem", "i(iiii)"}, {}
    };
    return link_stubs(module, t);
}

M3Result link_deflate_imports(IM3Module module)
{
    static const StubImport t[] = {
        {"deflate_file", "i(ii)"}, {"deflate_mem_to_file", "i(iii)"}, {"deflate_mem", "i(iiii)"}, {}
    };
    return link_stubs(module, t);
}

void wasm_reset_gamma(void) {}      // lives in the (stubbed) LED imports

// ------------------------------------------------------- the real run code
// wasm_run() is static in the firmware, so its file is compiled as part of
// this one. Its FreeRTOS task is never created (shim/freertos/task.h).

#include "../../firmware/src/wasm/wasm_wrapper.cpp"

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <fs-root> </path/inside/fs-root.wasm>\n", argv[0]);
        return 2;
    }
    snprintf(fwhost_fs_root, sizeof(fwhost_fs_root), "%s", argv[1]);
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);

    setup_wasm();
    wasm_run(argv[2]);

    fflush(stdout);
    return 0;
}
