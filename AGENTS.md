# AGENTS.md

This file provides guidance to AI coding agents working with code in this repository.

## Project Overview

ConeZ is an ESP32-S3 embedded system that powers networked LED light displays on traffic cones for fun and interesting playa events. It combines GPS positioning, LoRa radio communication, IMU sensors, and RGB LED control via user scripts (BASIC or WebAssembly) running on dedicated FreeRTOS threads.

## Build Commands

All commands run from the `firmware/` directory. The build system is PlatformIO with Arduino framework.

If `pio` is not on `PATH`, use the local PlatformIO virtualenv binary:

```bash
/home/ryan/.platformio/penv/bin/pio run -e conez-v0-1
```

or export it for the current shell session:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

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
└── util/                     Utilities, shell, sun calc, LUT
```

PlatformIO's `-I src/<dir>` flags in `platformio.ini` make all headers includable by basename (e.g. `#include "gps.h"`).

There are standalone test projects under `tests/` (psram_test, thread_test) — each is a separate PlatformIO project built/flashed independently from its own directory.

## Architecture

### Dual-Core Threading Model

FreeRTOS on ESP32-S3 uses **preemptive scheduling with time slicing** (`configUSE_PREEMPTION=1`, `configUSE_TIME_SLICING=1`, 1000 Hz tick). Equal-priority tasks on the same core are time-sliced at 1ms intervals. Higher-priority tasks preempt immediately.

| Task | Core | Priority | Stack | Source | Lifecycle |
|------|------|----------|-------|--------|-----------|
| loopTask | 1 | 1 | 8192 | Arduino `loop()` in `main.cpp` | Always running |
| ShellTask | 1 | 1 | 8192 | `shell_task_fun` in `main.cpp` | Always running |
| led_render | 1 | 2 | 4096 | `led_task_fun` in `led/led.cpp` | Always running |
| BasicTask | any | 1 | 16384 | `basic_task_fun` in `basic/basic_wrapper.cpp` | Created on first script |
| WasmTask | any | 1 | 16384 | `wasm_task_fun` in `wasm/wasm_wrapper.cpp` | Created on first script |

**Core 1 tasks:**
- **loopTask** — Hardware polling: LoRa RX, GPS parsing, sensor polling, WiFi/HTTP, NTP, cue engine, LED heartbeat blink. All non-blocking polling, yields via `vTaskDelay(1)` each iteration.
- **ShellTask** — CLI input processing (`prepInput`), command execution, interactive apps (editor, game). Yields via `vTaskDelay(1)` each iteration. Blocking commands (editor, game) run here without blocking loopTask.
- **led_render** — Calls `FastLED.show()` at ~30 FPS when dirty, at least 1/sec unconditionally. Priority 2 preempts both loopTask and ShellTask.

**Unpinned tasks (created on first use, not at boot):**
- **BasicTask** — BASIC interpreter. `tskNO_AFFINITY` — scheduler places on whichever core has bandwidth (typically core 0 since core 1 is busier).
- **WasmTask** — WASM interpreter. Same as BasicTask.

**Critical rules:**
- After `setup()`, only `led_render` calls `FastLED.show()`. All other code writes to `leds1`-`leds4` and calls `led_show()` to set the dirty flag. During `setup()` only, `led_show_now()` may be used.
- `vTaskDelay()` calls in task loops are for CPU efficiency (avoid busy spin), not for yielding — FreeRTOS preempts at 1ms ticks regardless. Do NOT add yields inside `editor_draw()` or similar atomic screen-update functions; it would cause visible screen tearing.

### USB Serial (HWCDC) Thread Safety

**The ESP32-S3 USB CDC (HWCDC) has a known bug that causes data corruption when accessed from multiple cores.** This is a hardware/driver-level race condition in the HWCDC TX FIFO, documented in multiple open issues against the Espressif Arduino core ([#9378](https://github.com/espressif/arduino-esp32/issues/9378), [#10836](https://github.com/espressif/arduino-esp32/issues/10836), [#11959](https://github.com/espressif/arduino-esp32/issues/11959)). Espressif's official USB Serial/JTAG documentation does not mention this limitation.

**Root cause:** The HWCDC has a 64-byte TX FIFO drained by a USB interrupt handler. `Serial.begin()` in `setup()` binds that interrupt to the calling core (core 1 on Arduino). When task code calls `Serial.write()` from a different core, the write and the interrupt handler access the FIFO registers simultaneously — a true hardware race that no software mutex can prevent, because the interrupt doesn't respect FreeRTOS mutexes. Symptoms: garbled output, lost bytes, interleaved fragments from different `print()` calls.

**The fix:** Pin all tasks that write to Serial to core 1, the same core as the HWCDC interrupt. On a single core, the interrupt *preempts* the task (cleanly saving/restoring state) rather than *racing* it on a separate core. This is the model the FIFO hardware was designed for.

**Mandatory constraints:**
1. **ShellTask MUST be pinned to core 1** (`xTaskCreatePinnedToCore(..., 1)`) — same core as loopTask and the HWCDC interrupt handler. This is the primary fix for the corruption bug.
2. **All Serial output after `setup()` must go through `printfnl()`** which holds `print_mutex`. Direct `Serial.print()` calls from any task will bypass the mutex and corrupt output.
3. **DualStream and TelnetServer both override `write(const uint8_t*, size_t)`** for efficient bulk writes. The per-byte `write(uint8_t)` override still exists for compatibility.
4. **ESP-IDF component logging is suppressed** via `esp_log_level_set("*", ESP_LOG_NONE)` before ShellTask creation. With `ARDUINO_USB_CDC_ON_BOOT=1`, ESP-IDF log output shares the same USB CDC and bypasses `print_mutex`.
5. **Interactive apps** (editor, game) use `setInteractive(true)` which makes `printfnl()` return immediately without writing. The app then writes directly to the stream under `getLock()`/`releaseLock()`. This is safe because ShellTask is the only task writing to Serial at that point.

**If you add a new FreeRTOS task that needs Serial output:** Route it through `printfnl()`. Never call `Serial.print()` directly. If the task must be on core 0 (e.g., for a peripheral interrupt), use `printfnl()` — it acquires the mutex, so the actual `Serial.write()` happens on whatever core the calling task is on. However, this means the HWCDC FIFO is being written from core 0 while its interrupt handler runs on core 1, which can trigger the corruption bug. If this becomes a problem, the next step is a FreeRTOS StreamBuffer to queue output and have a dedicated core 1 task drain it to Serial.

### Thread Communication

- **printManager** (`console/printManager.cpp/h`): Mutex-protected logging. All text output outside of `setup()` must go through `printfnl()`. Each message has a `source_e` tag (SOURCE_BASIC, SOURCE_WASM, SOURCE_GPS, SOURCE_LORA, etc.) for filtering. The mutex also protects shell suspend/resume (erasing and redrawing the input line around background output).
- **Params** (`set_basic_param` / `get_basic_param`): 16-slot integer array for passing values between main loop and scripting runtimes. Accessed via `GETPARAM(id)` in BASIC or `get_param(id)`/`set_param(id,val)` in WASM.
- **Script loading** (`set_script_program`): Auto-detects `.bas` vs `.wasm` by extension, routes to the appropriate runtime's mutex-protected queue. Creates the interpreter task on first use (lazy initialization).

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
| `wasm_imports_math.cpp` | 12 float + 12 double transcendental wrappers + 3 curve functions (lerp, larp, larpf) |
| `wasm_format.cpp` | wasm_vformat(), wasm_vsscanf(), host_printf/snprintf/sscanf |
| `wasm_imports_string.cpp` | BASIC string pool allocator + 19 string host imports (`basic_str_*`) |

Each file contains its wrapper functions and a `link_*_imports()` function that registers them. Adding a new host import is a single-file edit: add the `m3ApiRawFunction` wrapper and a `m3_LinkRawFunction` call in the same file's link function.

**Naming convention:** Host imports that are useful to C and Rust modules use short names matching libc conventions (e.g. `sinf`, `get_temp`, `led_fill`). Imports that exist only to support the BASIC-to-WASM compiler and aren't useful for C/Rust modules use a `basic_` prefix (e.g. `basic_str_alloc`, `basic_str_concat`, `basic_str_hex`).

**Host imports** (module `"env"`): LED (`led_set_pixel`, `led_fill`, `led_show`, `led_count`, `led_set_pixel_hsv`, `led_fill_hsv`, `hsv_to_rgb`, `rgb_to_hsv`, `led_gamma8`, `led_set_gamma`, `led_set_buffer`, `led_shift`, `led_rotate`, `led_reverse`), GPIO (`pin_set`, `pin_clear`, `pin_read`, `analog_read`), GPS (`get_lat`, `get_lon`, `get_alt`, `get_speed`, `get_dir`, `gps_valid`), GPS origin/geometry (`get_origin_lat`, `get_origin_lon`, `has_origin`, `origin_dist`, `origin_bearing`), IMU (`get_roll`, `get_pitch`, `get_yaw`, `get_acc_x/y/z`, `imu_valid`), environment (`get_temp`, `get_humidity`, `get_brightness`, `get_bat_voltage`, `get_solar_voltage`), sun position (`get_sunrise`, `get_sunset`, `sun_valid`, `is_daylight`), cue engine (`cue_playing`, `cue_elapsed`), date/time (`get_year`, `get_month`, `get_day`, `get_hour`, `get_minute`, `get_second`, `get_day_of_week`, `get_day_of_year`, `get_is_leap_year`, `time_valid`, `get_epoch_ms`, `millis`, `millis64`, `delay_ms`), event sync (`wait_pps`, `wait_param`), params (`get_param`, `set_param`, `should_stop`), output (`print_i32`, `print_f32`, `print_i64`, `print_f64`, `print_str`), math float (`sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`, `powf`, `expf`, `logf`, `log2f`, `fmodf`), math double (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pow`, `exp`, `log`, `log2`, `fmod`), curve (`lerp`, `larp`, `larpf`), printf/scanf (`host_printf`, `host_snprintf`, `host_sscanf`), LUT (`lut_load`, `lut_get`, `lut_size`, `lut_set`, `lut_save`, `lut_check`), file I/O (`file_open`, `file_close`, `file_read`, `file_write`, `file_size`, `file_seek`, `file_tell`, `file_exists`, `file_delete`, `file_rename`). Full reference in `documentation/wasm-api.txt`.

**WASI support** (module `"wasi_snapshot_preview1"`): Minimal WASI stubs so `println!()` works in Rust when compiled with `--target=wasm32-wasip1`. `fd_write` handles stdout/stderr via `printfnl(SOURCE_WASM, ...)`. `fd_seek`, `fd_close` return EBADF. `proc_exit` sets the stop flag. All WASM output uses the `SOURCE_WASM` debug channel (filterable independently from BASIC via `debug.wasm` config key or `debug WASM` CLI command).

**Standard C library support:** The `conez_api.h` header provides `printf`/`sprintf`/`snprintf`/`sscanf` (host-imported by default, or define `CONEZ_PRINTF_INLINE` for a self-contained ~1KB inline printf), `puts`/`putchar`, memory functions (`memcpy`/`memset`/`memmove` via WASM bulk-memory instructions, `memcmp`), string functions (`strlen`, `strcmp`, `strncmp`, `strchr`), parsing (`atoi`, `atof`, `strtol`, `strtof`), `va_list` macros, `size_t`, `NULL`, `bool`/`true`/`false`, and `M_PI`. WASM-native math in both float and double (`sqrtf`/`sqrt`, `fabsf`/`fabs`, `floorf`/`floor`, `ceilf`/`ceil`, `truncf`/`trunc`, `fminf`/`fmin`, `fmaxf`/`fmax`) compile to single instructions. Utility helpers: `clamp`, `clamp255`, `abs_i`, `abs`, `map_range`, `sin256`.

**LUT access:** WASM modules share the same LUT globals (`pLUT`, `lutSize`, `currentLUTIndex`) as BASIC. LUT functions are protected by a FreeRTOS mutex (initialized via `lutMutexInit()` in `setup()`) so they are safe to call from any task. LUT core functions are in `util/lut.h`/`lut.cpp`.

**File I/O:** WASM modules can open up to 4 files simultaneously on LittleFS. Paths must start with `/`, `..` traversal is blocked, and `/config.ini` is protected. All open file handles are automatically closed when the WASM module exits.

**Automatic yield:** The `m3_Yield()` hook fires every ~10K WASM Call opcodes, yielding to FreeRTOS and checking the stop flag. This prevents runaway modules from starving the watchdog even if they never call `delay_ms()`. Explicit `delay_ms()` calls are still recommended in loops for predictable timing.

**Authoring modules (C):** Header at `tools/wasm/conez_api.h` declares all imports and provides libc-equivalent functions. Two compilation options:

Option 1 — **c2wasm** (self-contained, no external dependencies):
```bash
cd tools/c2wasm && make          # build the compiler once
./c2wasm ../wasm/examples/rgb_cycle.c -o rgb_cycle.wasm
```
c2wasm compiles a C subset (int/float/double/void/char/long long, unsigned int/unsigned long long/uint8_t..uint64_t/size_t, short/signed/_Bool/bool, const enforcement, if/else/for/while/do/switch, full operator precedence, comma operator, ternary, multidimensional arrays with chained subscripts and compound assignment, nested/designated array initializers, pointer arithmetic/dereference/address-of/increment with element-size scaling (including local/global scalar linear-memory spill-slot addressing), `#include "conez_api.h"`, `#define`/`#undef` (with line continuation), `#ifdef`/`#ifndef`/`#elif`/`#else`/`#endif`/`#if` with full constant expressions, `#error`/`#warning`, printf, host_snprintf, file I/O, malloc/free/calloc/realloc imports, WASM-native sqrtf/fabsf/floorf/ceilf/truncf/fminf/fmaxf + f64 equivalents, f64 math sin/cos/tan/etc.) directly to WASM. No clang, LLVM, or SDK needed. Test suite: `make test` (currently 142 tests). See `documentation/c2wasm.txt` for the full reference.

Option 2 — **clang** (full C, optimized output):
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

**Offline compiler tools:** `tools/bas2wasm/` (BASIC→WASM) and `tools/c2wasm/` (C→WASM) are self-contained compilers with no external dependencies beyond libc. Both share `tools/buildnum.txt` for version tracking. Build each with `make` from its directory. See `documentation/bas2wasm.txt` and `documentation/c2wasm.txt` for full references.

### Desktop Simulator

Qt6-based simulator in `simulator/conez/` that runs WASM programs on a Linux desktop without hardware. Provides an LED visualizer (4 channels, 30 FPS), interactive sensor sliders, and a console. Uses the same vendored wasm3 interpreter and the same 148 host imports as the firmware, so programs that work in the simulator work on hardware.

```bash
# Build (requires Qt6 Widgets, CMake >= 3.16, C++17)
cd simulator/conez && mkdir build && cd build
cmake -DCMAKE_PREFIX_PATH=/usr/lib/x86_64-linux-gnu/cmake ..
make -j$(nproc)

# Run
./conez-simulator
./conez-simulator --leds 100 --bas2wasm /path/to/bas2wasm
./conez-simulator test.bas               # run a script on startup
```

**Data directory:** `simulator/conez/data/` ships example scripts copied from `firmware/data/`. Auto-detected at startup relative to the binary; overridable with `--sandbox`. CLI commands (`dir`, `list`, `del`, `ren`) and WASM file I/O operate in this directory. Bare filenames in `run` resolve here.

**Console commands:** `?`, `run`, `stop`, `open`, `dir`, `del`, `list`, `ren`, `param`, `led`, `sensors`, `time`, `uptime`, `version`, `wasm`. These mirror the firmware CLI; hardware-only commands (config, cue, debug, gps, load, lora, mem, ps, reboot, tc, wifi) are not available.

**Source layout:** `src/gui/` (LED strip, console, sensor panel widgets), `src/state/` (LED buffers, sensor mock, config), `src/wasm/` (runtime + 10 import files mirroring firmware), `src/worker/` (QThread for WASM, QProcess for compilation). Vendored wasm3 in `thirdparty/wasm3/source/`. Example data in `data/`.

**Threading:** Main thread runs Qt event loop and all widgets. WasmWorker QThread runs the wasm3 interpreter. Communication via Qt signals/slots with queued connections. Shared state (LedState, SensorState) is mutex-protected.

**Differences from firmware:** Sensors come from GUI sliders (not real hardware). GPIO stubs log to console. No LoRa, WiFi, PSRAM, or cue timeline player. File I/O uses the data directory. DateTime uses host system clock.

See `documentation/simulator.txt` for full reference.

**Keeping firmware and simulator in sync:** The simulator mirrors the firmware's WASM host imports and must be updated whenever the firmware API changes. When making changes to the firmware, apply the corresponding change to the simulator:

| Firmware change | Simulator counterpart |
|---|---|
| Add/modify host import in `firmware/src/wasm/wasm_imports_*.cpp` | Update matching `simulator/conez/src/wasm/sim_wasm_imports_*.cpp` |
| Add new import category file | Create new `sim_wasm_imports_*.cpp`, add to `CMakeLists.txt`, add `link_*_imports()` call in `sim_wasm_runtime.cpp` |
| Change `m3_LinkRawFunction` signature (type string) | Change the same signature in the simulator's link function |
| Add/change sensor field in firmware | Add field to `SensorMock` in `sensor_state.h`, add slider in `sensor_panel.cpp` |
| Update `conez_api.h` | No simulator change needed (header is for module authors, not the runtime) |
| Change wasm3 version | Re-vendor: copy `firmware/.pio/libdeps/conez-v0-1/Wasm3/src/*.{c,h}` to `simulator/conez/thirdparty/wasm3/source/` |
| Add/change CLI command | Add/change in `mainwindow.cpp` `onCommand()` dispatch and corresponding `cmd*()` method |
| Add/change data files in `firmware/data/` | Copy updated files to `simulator/conez/data/` |

The general rule: if a WASM program's behavior would differ between firmware and simulator after a change, both must be updated together.

### Mayhem (Cue-List Editor)

Avalonia/C# desktop application in `c_sharp/mayhem/` for authoring timeline-based lighting choreography. Users arrange effects (color gradients, FX presets, BASIC/WASM scripts, audio/video media) on a multi-channel timeline, set timing cues, and export bundled cue-lists for deployment to hardware. Projects save as `.clf` (JSON).

```bash
# Linux (Debian/Ubuntu)
sudo apt install dotnet-sdk-10.0 ffmpeg libavcodec-dev libavformat-dev \
    libswscale-dev libswresample-dev libavutil-dev
cd c_sharp/mayhem
FFMPEG_PATH=/usr/lib/x86_64-linux-gnu dotnet run --project Mayhem/Mayhem.csproj

# macOS (Homebrew)
brew install dotnet ffmpeg
cd c_sharp/mayhem
FFMPEG_PATH=/opt/homebrew/opt/ffmpeg/lib dotnet run --project Mayhem/Mayhem.csproj
```

**Dependencies:** .NET 10.0 SDK, Avalonia 11.3, FFmpeg libraries (audio/video decoding via FFmpeg.AutoGen bindings).

**Source layout:** `Models/` (Project, Channel, Effect types, Cue), `ViewModels/` (MVVM), `Services/` (FFmpeg decoding, audio playback, project file I/O), `Converters/` (XAML value converters). Sample scripts in `Scripts/`.

See `c_sharp/mayhem/readme.md` for quick-start instructions.

### Build Flags

`INCLUDE_BASIC` and `INCLUDE_WASM` in `platformio.ini` build_flags control which scripting runtimes are compiled in. Both are enabled by default. Remove either flag to exclude that runtime and save flash space (~67KB for wasm3).

### Board Abstraction

`board.h` defines board-specific hardware via compile-time `#ifdef`:
- `BOARD_CONEZ_V0_1` — Custom PCB, SX1268 LoRa, GPS, buzzer, 8MB aux SPI PSRAM (LY68L6400SLIT)
- `BOARD_HELTEC_LORA32_V3` — Heltec dev board, SX1262 LoRa, no GPS, no PSRAM

Pin assignments for the ConeZ PCB are in `board.h`. LED buffer pointers and setup are in `led/led.h`/`led.cpp`; per-channel LED counts are runtime-configurable via the `[led]` config section. The board is selected via build flags in `platformio.ini`.

### Key Hardware Interfaces

- **LoRa:** RadioLib, SX1262/SX1268 via SPI, configurable frequency/BW/SF/CR (defaults: 431.250 MHz, SF9, 500 kHz BW)
- **GPS:** ATGM336H (AT6558 chipset) via UART (9600 baud), parsed by TinyGPSPlus, with PPS pin for interrupt-driven timing (see Time System below). TX pin wired for PCAS configuration commands (`gps_send_nmea()` in `sensors/gps.cpp`).
- **LEDs:** FastLED WS2811 on 4 GPIO pins, BRG color order. Per-channel LED counts are configurable via `[led]` config section (default: 50 each). Buffers are dynamically allocated at boot. All FastLED interaction is centralized in `led/led.cpp`/`led.h`.
- **IMU:** MPU6500 on I2C 0x68 (custom driver in `lib/MPU9250_WE/`)
- **Temp:** TMP102 on I2C 0x48
- **PSRAM:** 8MB external SPI PSRAM on ConeZ PCB. See PSRAM Subsystem section below.
- **WiFi:** STA mode, SSID/password from config system, with ElegantOTA at `/update`
- **CLI:** ConezShell (`util/shell.cpp/h`) on DualStream — both USB Serial and Telnet (port 23) active simultaneously, all output to both. TelnetServer (`console/telnet.cpp/h`) handles IAC WILL ECHO + WILL SGA negotiation. Arrow keys, Home/End/Delete, Ctrl-A/E/U, 32-entry command history (PSRAM-backed ring buffer on ConeZ PCB, single-entry DRAM fallback on Heltec). ANSI color output when `SHELL_USE_ANSI` is defined. `CORE_DEBUG_LEVEL=0` in `platformio.ini` suppresses Arduino library log macros (`log_e`/`log_w`/etc.) at compile time — these bypass `esp_log_level_set()` and would corrupt HWCDC output. File commands auto-normalize paths (prepend `/` if missing) via `normalize_path()` in `main.h`. Full-screen text editor (`console/editor.cpp/h`) for on-device script editing. See `documentation/cli-commands.txt` for the full command reference.

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
| `BOARD_HAS_IMPROVISED_PSRAM` | ConeZ PCB v0.1 | External LY68L6400SLIT 8MB SPI PSRAM on GPIO 5/4/6/7 (CE/MISO/SCK/MOSI, FSPI bus). Accessed via SPI commands, not memory-mapped. |
| `BOARD_HAS_NATIVE_PSRAM` | Future boards | ESP-IDF memory-mapped PSRAM. Wraps `ps_malloc()`/`free()`. Addresses are real pointers. |
| Neither | Heltec LoRa32 V3 | All allocations silently fall back to the system heap (`malloc`/`free`). |

**Address types — `IS_ADDRESS_MAPPED(addr)`:** Returns true if the address is CPU-dereferenceable (>= `0x3C000000` — internal SRAM, native PSRAM, flash cache, heap). Returns false for improvised SPI-PSRAM virtual addresses (`0x10000000`–`0x107FFFFF`) which must go through `psram_read()`/`psram_write()`. All `psram_*` functions accept either address type and pick the fast path (direct memcpy) vs SPI path automatically.

**Allocator:** `psram_malloc(size)` returns a `uint32_t` address (not a pointer), or 0 on failure (like standard `malloc` returning NULL). All allocations are 4-byte aligned. The allocator uses a **fixed-size block table** in internal SRAM — `PSRAM_ALLOC_ENTRIES` slots (default 64), each 12 bytes. Each allocation or free-space region consumes one slot. When the table is full or PSRAM is unavailable, `psram_malloc()` transparently falls back to the system heap. Fallback allocations are tracked in a separate table so `psram_free()` and `psram_free_all()` can release them. `psram_free(0)` is a safe no-op. `psram_free_all()` releases everything — PSRAM allocations, cache contents, and fallback heap allocations.

**Read/write:** Typed accessors (`psram_read8`/`16`/`32`/`64`, `psram_write8`/`16`/`32`/`64`) and bulk (`psram_read`, `psram_write`). On improvised PSRAM, bulk transfers are chunked to respect the chip's 8µs tCEM limit and routed through the DRAM page cache. Bounds-checked against `BOARD_PSRAM_SIZE`. Memory operations (`psram_memset`, `psram_memcpy`, `psram_memcmp`) accept mixed address types.

**DRAM page cache:** Write-back LRU cache for improvised SPI PSRAM (no-op on native/stubs). Default 16 pages × 512 bytes = ~8KB DRAM. Configurable at compile time via `PSRAM_CACHE_PAGES` and `PSRAM_CACHE_PAGE_SIZE`. Set `PSRAM_CACHE_PAGES` to 0 to disable.

**Thread safety:** All public functions are protected by a recursive FreeRTOS mutex. Safe to call from any task. The memory test (`psram_test`) runs without the mutex and requires exclusive access — refuses to run if any allocations exist.

**CLI:** `psram` shows status (size, used/free, contiguous, alloc slots used/max, cache hit rate, SPI clock) plus a 64-character allocation map (`-` free, `+` partial, `*` full, ANSI colored). `psram test` runs a full memory test with throughput benchmark (frees/re-allocates shell history ring buffer for exclusive PSRAM access). `psram test forever` loops until error or keypress. `psram freq <MHz>` changes SPI clock at runtime (5–80 MHz) for benchmarking. `mem` also includes a PSRAM summary.

**Hardware details (ConeZ PCB):** LY68L6400SLIT (Lyontek), 64Mbit/8MB, 23-bit address, SPI-only wiring (no quad), FSPI bus (SPI2) on GPIO 5/4/6/7 (CE/MISO/SCK/MOSI). These are **GPIO matrix** routed (not IOMUX), so the documented reliable max is 26 MHz; IOMUX pins (GPIO 10–13) would allow 80 MHz. Default boot clock: 40 MHz (APB/2). Read command auto-selects: slow read `0x03` (no wait, max 33 MHz) vs fast read `0x0B` (8 wait cycles, max 133 MHz). 8µs tCEM max per CE# assertion — driver chunks transfers to stay within budget. 1KB page size. Datasheet: `hardware/datasheets/LY68L6400SLIT.pdf`.

**SPI clock and tCEM:** ESP32-S3 SPI clock = APB (80 MHz) / integer N. The driver computes the actual achieved frequency and sizes chunks accordingly. At low frequencies, the 4-byte command overhead (cmd + 3 addr) dominates — at 5 MHz only 1 byte of payload fits per tCEM window.

**Runtime SPI clock changes (`psram freq`):** The Arduino SPI library's `beginTransaction()` acquires two locks that are never released (we own the FSPI bus exclusively and never call `endTransaction()`): the Arduino `paramLock` and the HAL-level `spi->lock`. Both are held by loopTask from `psram_setup()`. Since `psram freq` runs on ShellTask, it cannot use any Arduino SPI API — `setFrequency()`, `beginTransaction()`, `endTransaction()`, and even the HAL's `spiSetClockDiv()` all try to acquire one of these permanently-held locks, causing deadlock. The fix: `psram_change_freq()` writes the SPI2 clock register directly via `GPSPI2.clock.val` (from `soc/spi_struct.h`), computing the divider with `spiFrequencyToClockDiv()`. This bypasses all lock layers and is safe under `psram_mutex`. Note: `spiFrequencyToClockDiv()` and the `SPISettings` constructor compute the same dividers on this framework version; earlier ESP32 Arduino versions had a special-case APB/2 path in `SPISettings` that `spiFrequencyToClockDiv()` missed.

**Throughput benchmarks (ConeZ PCB v0.1, 8MB, `psram test`):**

Baseline measured with single-task architecture (shell in loopTask). Current numbers are ~17% lower due to ShellTask overhead (extra `vTaskDelay` yields on busier core 1, `printfnl` suspend/resume during progress output). Frequency scaling ratios are consistent between both.

| Actual SPI Clock | Read KB/s | Write KB/s | Current Read | Current Write | Notes |
|---|---|---|---|---|---|
| 5.00 MHz | 88 | 93 | | | 1 byte payload/chunk — overhead-dominated |
| 6.67 MHz | 97 | 103 | | | |
| 8.00 MHz | 346 | 374 | | | |
| 10.00 MHz | 498 | 545 | | | |
| 13.33 MHz | 705 | 786 | | | |
| 16.00 MHz | 917 | 1,030 | | | |
| 20.00 MHz | 1,233 | 1,414 | | | |
| 26.67 MHz | 1,603 | 1,893 | | | Max documented for GPIO matrix routing |
| 40.00 MHz | 2,216 | 2,800 | 1,855 | 2,321 | Default boot clock (APB/2) |
| 80.00 MHz | 3,327 | 4,534 | 2,736 | 3,687 | APB/1 — requires IOMUX pins for reliability |

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
