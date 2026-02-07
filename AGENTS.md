# AGENTS.md

This file provides guidance to AI coding agents working with code in this repository.

## Project Overview

ConeZ is an ESP32-S3 embedded system that powers networked LED light displays on traffic cones for fun and interesting playa events. It combines GPS positioning, LoRa radio communication, IMU sensors, and RGB LED control via user scripts (BASIC or WebAssembly) running on dedicated FreeRTOS threads.

## Build Commands

All commands run from the `firmware/` directory. The build system is PlatformIO with Arduino framework.

```bash
# Build for ConeZ custom PCB (default)
pio run -e conez-v0-1

# Build for Heltec LoRa32 v3 dev board
pio run -e heltec_wifi_lora_32_V3

# Upload via USB
pio run -e conez-v0-1 --target upload

# Serial monitor
pio run -e conez-v0-1 --target monitor --monitor-speed 115200

# Upload LittleFS filesystem (scripts and data in firmware/data/)
pio run -e conez-v0-1 --target uploadfs
```

### Source Layout

All source files are under `firmware/src/`, organized into subdirectories:

```
src/
├── main.cpp, main.h         Entry point, setup/loop
├── board.h                   Pin definitions per board
├── config.cpp, config.h      INI config system
├── basic/                    BASIC interpreter + extensions
├── wasm/                     WASM runtime (wasm3)
├── console/                  CLI commands + printManager
├── http/                     Web server + OTA updates
├── lora/                     LoRa radio
├── sensors/                  GPS, IMU, temperature
├── led/                      LED driver (FastLED)
├── effects/                  Direct LED effects
├── cue/                      Cue timeline engine
├── psram/                    External SPI PSRAM driver
└── util/                     Utilities, sun calc, LUT
```

PlatformIO's `-I src/<dir>` flags in `platformio.ini` make all headers includable by basename (e.g. `#include "gps.h"`).

There are standalone test projects under `tests/` (psram_test, thread_test) — each is a separate PlatformIO project built/flashed independently from its own directory.

## Architecture

### Dual-Core Threading Model

- **Core 1 (main loop):** Hardware drivers — LoRa RX, GPS parsing, sensor polling, WiFi/HTTP, CLI shell, "direct effects" (speed-of-sound sync). Runs in `loop()` in `main.cpp`.
- **Core 1 (LED render task):** Dedicated FreeRTOS task (`led_task_fun` in `led/led.cpp`, 4KB stack, priority 2) that calls `FastLED.show()` at ~30 FPS when the dirty flag is set, and at least once per second unconditionally. This is the **only** place `FastLED.show()` is called after `setup()`.
- **Core 0 (BASIC task):** Dedicated FreeRTOS task (`basic_task_fun` in `basic/basic_wrapper.cpp`, 65KB stack) that runs user BASIC scripts. Guarded by `INCLUDE_BASIC` build flag.
- **Core 0 (WASM task):** Dedicated FreeRTOS task (`wasm_task_fun` in `wasm/wasm_wrapper.cpp`, 65KB stack) that runs `.wasm` modules via wasm3 interpreter. Guarded by `INCLUDE_WASM` build flag. Both tasks coexist on Core 0.

**Critical rule:** After `setup()`, only the LED render task calls `FastLED.show()`. All other code (BASIC scripts, WASM modules, effects, etc.) writes to the global LED buffers (`leds1`-`leds4`) and calls `led_show()` to set the dirty flag. During `setup()` only, `led_show_now()` may be used for immediate display.

### Thread Communication

- **printManager** (`console/printManager.cpp/h`): Mutex-protected logging. All text output outside of `setup()` must go through `printfnl()`. Each message has a `source_e` tag (SOURCE_BASIC, SOURCE_WASM, SOURCE_GPS, SOURCE_LORA, etc.) for filtering.
- **Params** (`set_basic_param` / `get_basic_param`): 16-slot integer array for passing values between main loop and scripting runtimes. Accessed via `GETPARAM(id)` in BASIC or `get_param(id)`/`set_param(id,val)` in WASM.
- **Script loading** (`set_script_program`): Auto-detects `.bas` vs `.wasm` by extension, routes to the appropriate runtime's mutex-protected queue.

### BASIC Interpreter Extensions

The BASIC interpreter (third-party, by Jerry Williams Jr, in `basic/basic.h`) is extended via callback-based hooks in `basic/basic_extensions.h`. Hardware access is abstracted through registered callback functions:

- `register_location_callback` — GPS data (origin lat/lon, current position, speed, direction)
- `register_imu_callback` — MPU6500 roll/pitch/yaw/acceleration
- `register_datetime_callback` — Date/time from GPS
- `register_sync_callback` — Event waiting (GPS PPS, timers, params)
- `register_env_callback` — Temperature, humidity, brightness

New BASIC functions are added by: (1) defining the C function that manipulates the stack (`*sp`), (2) adding a token `#define`, (3) registering it in `funhook_()` with argument count validation.

### WASM Runtime

WebAssembly interpreter via wasm3 in `wasm/`. Guarded by `INCLUDE_WASM` build flag. Loads `.wasm` binaries from LittleFS and runs them on Core 0. Entry point conventions: `setup()` + `loop()` (Arduino-style, loop runs until stopped), or `_start()` / `main()` (single-shot).

**Source files:** `wasm_wrapper.cpp/h` (state, m3_Yield, `wasm_run()`, FreeRTOS task, public API) dispatches to per-category import files via `wasm_internal.h`:

| File | Contents |
|---|---|
| `wasm_imports_led.cpp` | LED, HSV, gamma, bulk buffer, shift/rotate/reverse |
| `wasm_imports_sensors.cpp` | GPS, IMU, environment, battery/solar, sun, origin/geometry |
| `wasm_imports_datetime.cpp` | Epoch time, millis, delay, date/time accessors |
| `wasm_imports_gpio.cpp` | pin_set, pin_clear, pin_read, analog_read |
| `wasm_imports_system.cpp` | Params, should_stop, cue, event sync (wait_pps, wait_param) |
| `wasm_imports_file.cpp` | File I/O (open/close/read/write/seek/tell/exists/delete/rename) |
| `wasm_imports_io.cpp` | Print (i32/f32/i64/f64/str), WASI stubs, LUT |
| `wasm_imports_math.cpp` | 12 float + 12 double transcendental wrappers |
| `wasm_format.cpp` | wasm_vformat(), wasm_vsscanf(), host_printf/snprintf/sscanf |

Each file contains its wrapper functions and a `link_*_imports()` function that registers them. Adding a new host import is a single-file edit: add the `m3ApiRawFunction` wrapper and a `m3_LinkRawFunction` call in the same file's link function.

**Host imports** (module `"env"`): LED (`led_set_pixel`, `led_fill`, `led_show`, `led_count`, `led_set_pixel_hsv`, `led_fill_hsv`, `hsv_to_rgb`, `rgb_to_hsv`, `led_gamma8`, `led_set_gamma`, `led_set_buffer`, `led_shift`, `led_rotate`, `led_reverse`), GPIO (`pin_set`, `pin_clear`, `pin_read`, `analog_read`), GPS (`get_lat`, `get_lon`, `get_alt`, `get_speed`, `get_dir`, `gps_valid`), GPS origin/geometry (`get_origin_lat`, `get_origin_lon`, `has_origin`, `origin_dist`, `origin_bearing`), IMU (`get_roll`, `get_pitch`, `get_yaw`, `get_acc_x/y/z`, `imu_valid`), environment (`get_temp`, `get_humidity`, `get_brightness`, `get_bat_voltage`, `get_solar_voltage`), sun position (`get_sunrise`, `get_sunset`, `sun_valid`, `is_daylight`), cue engine (`cue_playing`, `cue_elapsed`), date/time (`get_year`, `get_month`, `get_day`, `get_hour`, `get_minute`, `get_second`, `get_day_of_week`, `get_day_of_year`, `get_is_leap_year`, `time_valid`, `get_epoch_ms`, `millis`, `delay_ms`), event sync (`wait_pps`, `wait_param`), params (`get_param`, `set_param`, `should_stop`), output (`print_i32`, `print_f32`, `print_i64`, `print_f64`, `print_str`), math float (`sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`, `powf`, `expf`, `logf`, `log2f`, `fmodf`), math double (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pow`, `exp`, `log`, `log2`, `fmod`), printf/scanf (`host_printf`, `host_snprintf`, `host_sscanf`), LUT (`lut_load`, `lut_get`, `lut_size`, `lut_set`, `lut_save`, `lut_check`), file I/O (`file_open`, `file_close`, `file_read`, `file_write`, `file_size`, `file_seek`, `file_tell`, `file_exists`, `file_delete`, `file_rename`). Full reference in `documentation/wasm-api.txt`.

**WASI support** (module `"wasi_snapshot_preview1"`): Minimal WASI stubs so `println!()` works in Rust when compiled with `--target=wasm32-wasip1`. `fd_write` handles stdout/stderr via `printfnl(SOURCE_WASM, ...)`. `fd_seek`, `fd_close` return EBADF. `proc_exit` sets the stop flag. All WASM output uses the `SOURCE_WASM` debug channel (filterable independently from BASIC via `debug.wasm` config key or `debug WASM` CLI command).

**Standard C library support:** The `conez_api.h` header provides `printf`/`sprintf`/`snprintf`/`sscanf` (host-imported by default, or define `CONEZ_PRINTF_INLINE` for a self-contained ~1KB inline printf), `puts`/`putchar`, memory functions (`memcpy`/`memset`/`memmove` via WASM bulk-memory instructions, `memcmp`), string functions (`strlen`, `strcmp`, `strncmp`, `strchr`), parsing (`atoi`, `atof`, `strtol`, `strtof`), `va_list` macros, `size_t`, `NULL`, `bool`/`true`/`false`, and `M_PI`. WASM-native math in both float and double (`sqrtf`/`sqrt`, `fabsf`/`fabs`, `floorf`/`floor`, `ceilf`/`ceil`, `truncf`/`trunc`, `fminf`/`fmin`, `fmaxf`/`fmax`) compile to single instructions. Utility helpers: `clamp`, `clamp255`, `abs_i`, `abs`, `map_range`, `sin256`.

**LUT access:** WASM modules share the same LUT globals (`pLUT`, `lutSize`, `currentLUTIndex`) as BASIC. LUT functions are protected by a FreeRTOS mutex (initialized via `lutMutexInit()` in `setup()`) so they are safe to call from any task. LUT core functions are in `util/lut.h`/`lut.cpp`.

**File I/O:** WASM modules can open up to 4 files simultaneously on LittleFS. Paths must start with `/`, `..` traversal is blocked, and `/config.ini` is protected. All open file handles are automatically closed when the WASM module exits.

**Automatic yield:** The `m3_Yield()` hook fires every ~10K WASM Call opcodes, yielding to FreeRTOS and checking the stop flag. This prevents runaway modules from starving the watchdog even if they never call `delay_ms()`. Explicit `delay_ms()` calls are still recommended in loops for predictable timing.

**Authoring modules (C):** Header at `tools/wasm/conez_api.h` declares all imports and provides libc-equivalent functions. Build with clang:

```bash
cd tools/wasm
clang --target=wasm32 -mbulk-memory -O2 -nostdlib \
  -Wl,--no-entry -Wl,--export=setup -Wl,--export=loop \
  -Wl,--allow-undefined \
  -I . -o module.wasm module.c
llvm-strip module.wasm
```

**Authoring modules (Rust):** Two approaches:
- **`#![no_std]`** targeting `wasm32-unknown-unknown` — minimal binary (~350 bytes). Declare ConeZ host imports as `extern "C"`, use `#[no_mangle]` exports. See `tools/wasm/examples/rust_rgb_cycle/` for a complete example.
- **std** targeting `wasm32-wasip1` — `println!()` works via WASI `fd_write` but adds ~35KB of formatting overhead. See `tools/wasm/examples/rust_rainbow/`.

Both use `cdylib` crate type, `opt-level = "z"`, LTO, strip, `panic = "abort"`. Post-process with `wasm-opt --enable-bulk-memory -Oz` to reduce binary size.

See `tools/wasm/examples/` for sample modules (C and Rust). Use `make -C tools/wasm` to build all examples and `make -C tools/wasm install` to copy them to `firmware/data/` for LittleFS upload.

### Build Flags

`INCLUDE_BASIC` and `INCLUDE_WASM` in `platformio.ini` build_flags control which scripting runtimes are compiled in. Both are enabled by default. Remove either flag to exclude that runtime and save flash space (~67KB for wasm3).

### Board Abstraction

`board.h` defines board-specific hardware via compile-time `#ifdef`:
- `BOARD_CONEZ_V0_1` — Custom PCB, SX1268 LoRa, GPS, buzzer, 8MB aux SPI PSRAM (LY68L6400SLIT)
- `BOARD_HELTEC_LORA32_V3` — Heltec dev board, SX1262 LoRa, no GPS, no PSRAM

Pin assignments for the ConeZ PCB are in `board.h`. LED buffer pointers and setup are in `led/led.h`/`led.cpp`; per-channel LED counts are runtime-configurable via the `[led]` config section. The board is selected via build flags in `platformio.ini`.

### Key Hardware Interfaces

- **LoRa:** RadioLib, SX1262/SX1268 via SPI, configurable frequency/BW/SF/CR (defaults: 431.250 MHz, SF9, 500 kHz BW)
- **GPS:** TinyGPSPlus on UART (9600 baud), with PPS pin for interrupt-driven timing (see Time System below)
- **LEDs:** FastLED WS2811 on 4 GPIO pins, BRG color order. Per-channel LED counts are configurable via `[led]` config section (default: 50 each). Buffers are dynamically allocated at boot. All FastLED interaction is centralized in `led/led.cpp`/`led.h`.
- **IMU:** MPU6500 on I2C 0x68 (custom driver in `lib/MPU9250_WE/`)
- **Temp:** TMP102 on I2C 0x48
- **PSRAM:** 8MB external SPI PSRAM on ConeZ PCB. See PSRAM Subsystem section below.
- **WiFi:** STA mode, SSID/password from config system, with ElegantOTA at `/update`
- **CLI:** SimpleSerialShell, defaults to Telnet after setup; press any key on USB Serial to switch back

### Time System

Unified time API in `sensors/gps.h`/`gps.cpp` provides millisecond-precision epoch time across all board types. Consumers call `get_epoch_ms()` regardless of source.

**Tiered sources (higher priority wins):**
- **GPS + PPS** (time_source=2, ~1us accuracy) — ConeZ PCB only. PPS rising edge triggers `IRAM_ATTR` ISR that captures `millis()`. NMEA sentence (arriving ~100-200ms later) provides absolute time for the preceding edge. Epoch stored under `portMUX_TYPE` spinlock (64-bit not atomic on 32-bit Xtensa).
- **NTP** (time_source=1, ~10-50ms accuracy) — Any board with WiFi. Uses ESP32 SNTP via `configTime()`. NTP server configurable via `[system] ntp_server` config key.
- **None** (time_source=0) — `get_epoch_ms()` returns 0, consumers no-op.

**GPS staleness fallback:** If PPS stops arriving for >10 seconds (GPS loss), `ntp_loop()` downgrades `time_source` to 0, allowing NTP to take over if WiFi is available.

**API:** `get_epoch_ms()` (ms since Unix epoch), `get_time_valid()` (any source active), `get_time_source()` (0/1/2), `get_pps_flag()` (ISR edge flag, clear-on-read), `ntp_setup()`, `ntp_loop()`.

**Thread safety:** `epoch_at_pps` + `millis_at_pps` protected by `portMUX_TYPE` spinlock. `pps_edge_flag` also uses spinlock for atomic read-then-clear. 32-bit `pps_millis`/`pps_count` use `volatile` only (aligned 32-bit atomic on Xtensa).

### PSRAM Subsystem

Unified memory API in `psram/psram.h`/`psram.cpp` that works across all board configurations. Callers use `psram_malloc()`/`psram_free()`/`psram_read()`/`psram_write()` without `#ifdefs` — the implementation adapts to the hardware at compile time.

**Board configurations (compile-time, via `board.h` defines):**

| Define | Board | Behavior |
|---|---|---|
| `BOARD_HAS_IMPROVISED_PSRAM` | ConeZ PCB v0.1 | External LY68L6400SLIT 8MB SPI PSRAM on GPIO 4/5/6/7 (FSPI bus). Accessed via SPI commands, not memory-mapped. |
| `BOARD_HAS_NATIVE_PSRAM` | Future boards | ESP-IDF memory-mapped PSRAM. Wraps `ps_malloc()`/`free()`. Addresses are real pointers. |
| Neither | Heltec LoRa32 V3 | All allocations silently fall back to the system heap (`malloc`/`free`). |

**Address types — `IS_ADDRESS_MAPPED(addr)`:** Returns true if the address is CPU-dereferenceable (>= `0x3C000000` — internal SRAM, native PSRAM, flash cache, heap). Returns false for improvised SPI-PSRAM virtual addresses (`0x10000000`–`0x107FFFFF`) which must go through `psram_read()`/`psram_write()`. All `psram_*` functions accept either address type and pick the fast path (direct memcpy) vs SPI path automatically.

**Allocator:** `psram_malloc(size)` returns a `uint32_t` address (not a pointer), or 0 on failure (like standard `malloc` returning NULL). All allocations are 4-byte aligned. The allocator uses a **fixed-size block table** in internal SRAM — `PSRAM_ALLOC_ENTRIES` slots (default 64), each 12 bytes. Each allocation or free-space region consumes one slot. When the table is full or PSRAM is unavailable, `psram_malloc()` transparently falls back to the system heap. Fallback allocations are tracked in a separate table so `psram_free()` and `psram_free_all()` can release them. `psram_free(0)` is a safe no-op. `psram_free_all()` releases everything — PSRAM allocations, cache contents, and fallback heap allocations.

**Read/write:** Typed accessors (`psram_read8`/`16`/`32`/`64`, `psram_write8`/`16`/`32`/`64`) and bulk (`psram_read`, `psram_write`). On improvised PSRAM, bulk transfers are chunked to respect the chip's 8µs tCEM limit and routed through the DRAM page cache. Bounds-checked against `BOARD_PSRAM_SIZE`. Memory operations (`psram_memset`, `psram_memcpy`, `psram_memcmp`) accept mixed address types.

**DRAM page cache:** Write-back LRU cache for improvised SPI PSRAM (no-op on native/stubs). Default 16 pages × 512 bytes = ~8KB DRAM. Configurable at compile time via `PSRAM_CACHE_PAGES` and `PSRAM_CACHE_PAGE_SIZE`. Set `PSRAM_CACHE_PAGES` to 0 to disable.

**Thread safety:** All public functions are protected by a recursive FreeRTOS mutex. Safe to call from any task. The memory test (`psram_test`) runs without the mutex and requires exclusive access — refuses to run if any allocations exist.

**CLI:** `psram` shows status (size, used/free, contiguous, alloc slots used/max, cache hit rate). `psram test` runs a full memory test with throughput benchmark. `psram test forever` loops until error or keypress. `mem` also includes a PSRAM summary.

**Hardware details (ConeZ PCB):** LY68L6400SLIT (Lyontek), 64Mbit/8MB, 23-bit address, SPI-only wiring (no quad), 33 MHz clock on FSPI bus (SPI2). Fast Read command `0x0B` with 8 wait cycles. 8µs tCEM max per CE# assertion — driver chunks transfers to stay within budget. 1KB page size. Datasheet: `hardware/datasheets/LY68L6400SLIT.pdf`.

### Configuration

INI-style config file (`/config.ini`) on LittleFS, loaded at boot. Descriptor-table-driven: `cfg_table[]` in `config.cpp` maps `{section, key, type, offset}` to `conez_config_t` struct fields. Covers WiFi, GPS origin, LoRa radio params, NTP server, timezone, debug defaults, callsign, and startup script. CLI commands: `config`, `config set`, `config unset`, `config reset`. See `documentation/config.txt` for full reference.

### Filesystem

LittleFS on 4MB flash partition. Stores BASIC scripts (`.bas`), WASM modules (`.wasm`), LUT data files (`LUT_N.csv`), binary cue files (`.cue`), and optionally `/config.ini`. The configured startup script (default `/startup.bas`) auto-executes on boot if present; the extension determines which runtime handles it.

### Firmware Versioning

`patch_firmware_ver.py` runs post-build to embed version/timestamp into the firmware binary. Configured via `custom_prog_*` fields in `platformio.ini`.

### Cue System

Epoch-time-synced LED cueing engine for music-synchronized light shows across geographically distributed cones. Uses `get_epoch_ms()` for timing — works with GPS+PPS (sub-ms sync between cones) or NTP fallback (adequate for single-cone or loose-sync shows). Binary `.cue` files on LittleFS contain a sorted timeline of cue entries (fill, blackout, stop, effect) that the engine walks during playback. Each cue supports group targeting (per-cone, per-group, bitmask) and spatial delay modes (radial ripple, directional waves) computed from GPS position.

**Firmware files:** `cue/cue.h`, `cue/cue.cpp`. Initialized via `cue_setup()` in `setup()`, ticked via `cue_loop()` in the main loop.

**Binary format:** 64-byte header (magic `0x43554530`, version, num_cues, record_size) followed by 64-byte cue entries sorted by start_ms. See `cue_header` and `cue_entry` structs in `cue/cue.h`.

**CLI commands:** `cue load <path>`, `cue start [ms]`, `cue stop`, `cue status`.

**Authoring tool:** `tools/cuetool.py` compiles human-editable YAML show descriptions into binary `.cue` files. Also supports dumping `.cue` files back to readable text. Requires PyYAML. See `tools/example_show.yaml` for the input format.

```bash
python3 tools/cuetool.py build tools/example_show.yaml -o firmware/data/show.cue
python3 tools/cuetool.py dump firmware/data/show.cue
```
