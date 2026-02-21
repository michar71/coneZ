# AGENTS.md

This file provides guidance to AI coding agents working with code in this repository.

## Project Overview

ConeZ is an ESP32-S3 embedded system that powers networked LED light displays on traffic cones for fun and interesting playa events. It combines GPS positioning, LoRa radio communication, IMU sensors, and RGB LED control via user scripts (BASIC or WebAssembly) running on dedicated FreeRTOS threads.

## Build Commands

All commands run from the `firmware/` directory. The build system is PlatformIO
with ESP-IDF framework and Arduino as an IDF component (`framework = espidf, arduino`).
This gives us full ESP-IDF SDK source compilation (with `sdkconfig.defaults` for
menuconfig overrides like `CONFIG_LWIP_STATS`) while keeping Arduino APIs (Serial,
WiFi, Wire, FastLED, etc.).

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

### ESP-IDF + Arduino Build System

The project uses `framework = espidf, arduino` in `platformio.ini`, which builds
ESP-IDF from source with Arduino as an IDF component. This replaces the stock
pre-compiled Arduino SDK, giving us full control over ESP-IDF configuration.

**Key files:**
- `platformio.ini` — pinned to `espressif32@6.12.0` (ESP-IDF 4.4.7)
- `sdkconfig.defaults` — ESP-IDF config overrides applied at build time
- `components/esp_littlefs/` — LittleFS filesystem component (v1.14.8 for IDF 4.4 compat)
- `main.cpp` — provides `app_main()` entry point (calls `initArduino()`, creates loopTask)

**sdkconfig.defaults** currently sets:
- `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` — `-Os` (IDF defaults to `-Og`; matches stock Arduino SDK)
- `CONFIG_COMPILER_STACK_CHECK_MODE_NORM=y` — stack canary overflow detection
- `CONFIG_LOG_DEFAULT_LEVEL_NONE=y` — compile out all IDF log strings from flash
- `CONFIG_BT_ENABLED=n` — Bluetooth explicitly disabled
- `CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y` — saves ~8KB IRAM
- `CONFIG_FREERTOS_HZ=1000` — required by Arduino framework
- `CONFIG_ESP_INT_WDT_TIMEOUT_MS=800` — longer interrupt WDT for PSRAM SPI transfers
- `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10` — longer task WDT for on-device compilation
- `CONFIG_ESP32_WIFI_DYNAMIC_RX_BUFFER_NUM=16` — halved from 32 (light MQTT/HTTP traffic)
- `CONFIG_ESP32_WIFI_DYNAMIC_TX_BUFFER_NUM=16` — halved from 32
- `CONFIG_LWIP_STATS=y` — LWIP protocol statistics (IP/TCP/UDP packet counters)
- `CONFIG_LWIP_TCP_SND_BUF_DEFAULT=2920` — halved TCP send buffer (2x MSS vs 4x)
- `CONFIG_LWIP_TCP_WND_DEFAULT=2920` — halved TCP receive window
- `CONFIG_PARTITION_TABLE_CUSTOM=y` — uses our `partitions.csv`

Additional build flag (not Kconfig): `-DMIB2_STATS=1` enables per-interface byte
counters on LWIP netif structs (`ifinoctets`/`ifoutoctets`).

See `documentation/sdkconfig-options.txt` for a full review of available options
including items considered but deferred (IPv6, SoftAP, WPA3, CPU freq, flash mode).

**ESP-IDF component: esp_littlefs.** LittleFS is not built into ESP-IDF 4.4 — it's
provided via `components/esp_littlefs/` (vendored from joltwallet/esp_littlefs
v1.14.8). The CMakeLists.txt was modified to use explicit source file lists instead
of `file(GLOB)` which PlatformIO's builder doesn't process correctly.

**Entry point:** We provide our own `extern "C" void app_main()` in `main.cpp`
instead of using `CONFIG_AUTOSTART_ARDUINO`. This gives us direct control over
loopTask stack size, priority, and core affinity. `app_main()` calls
`initArduino()` then creates loopTask pinned to core 1.

**Name collisions to watch for:**
- `mqtt_client.h` — ESP-IDF has its own; ours is named `conez_mqtt.h`
- `task.h` — use `#include "freertos/task.h"`, never bare `#include "task.h"`
- Stricter warnings: ESP-IDF enables `-Werror=format-truncation`,
  `-Werror=misleading-indentation`, `-Werror=class-memaccess`. Use `snprintf`
  with adequately sized buffers, put statements on separate lines after `if`,
  and avoid `memset` on C++ classes.

**First build** compiles the entire ESP-IDF from source (~3-5 minutes).
Subsequent incremental builds are fast (~5-20 seconds).

**For contributors:** The build is still `pio run` — no extra setup. PlatformIO
handles the ESP-IDF toolchain installation automatically. The `sdkconfig.defaults`
file is picked up without any manual `menuconfig` step.

There are standalone test projects under `tests/` (psram_test, thread_test) — each is a separate PlatformIO project built/flashed independently from its own directory.

### Library Dependencies

External libraries in `platformio.ini` `lib_deps`:

| Library | Version | Arduino required? | Notes |
|---------|---------|-------------------|-------|
| RadioLib | ^7.6.0 | No (HAL-based) | Uses `RadioLibHal` abstraction. Custom `EspHal` class in `lora/lora_hal.h` — raw GPSPI3 register access for SPI, ESP-IDF `driver/gpio.h` for GPIO, `esp_timer` for timing. No Arduino dependency. |
| FastLED | ^3.10.1 | No (optional) | `#ifdef ARDUINO` guards around `Arduino.h`. LED output uses direct ESP32 RMT registers (`platforms/esp/32/`), not Arduino GPIO. On IDF 4.4 uses RMT4 driver; I2S parallel driver requires IDF 5 (harmless warning at compile time). |
| sunset | ^1.1.7 | No | Pure C++ math (`<cmath>`, `<ctime>`). Zero platform dependency. |
| Wasm3 | ^0.5.0 | No | Pure C interpreter. Zero platform dependency. |

Local library in `lib/`:

| Library | Arduino required? | Notes |
|---------|-------------------|-------|
| *(none)* | — | MPU9250_WE was removed; replaced by custom MPU6500 driver in `sensors/mpu6500.cpp`/`.h` using ESP-IDF `driver/i2c.h`. |

**No library is a hard blocker for dropping Arduino.** RadioLib uses a custom ESP-IDF HAL (`lora_hal.h`). FastLED has IDF-native RMT code paths. Wasm3 and sunset are pure C/C++.

### Arduino Migration Path

The project currently uses `framework = espidf, arduino`. Arduino could be dropped
entirely (`framework = espidf` only) — all Arduino APIs have direct ESP-IDF
equivalents. This section documents the migration surface.

**Remaining Arduino API usage across firmware (~250 call sites):**

| Arduino API | ~Count | Files | ESP-IDF Replacement | Status |
|-------------|--------|-------|---------------------|--------|
| Serial (print/read/write) | 135 | 9 | UART driver or USB CDC (`tinyusb`). HWCDC core-1 constraint remains. | Pending |
| LittleFS / File | 77 | 15 | Already an IDF component (`esp_littlefs`). Switch to POSIX `open()`/`read()`/`write()`/`stat()`. | Pending |
| WiFi (WiFi.begin/status) | 39 | 6 | `esp_wifi.h` (already included alongside Arduino WiFi) | Pending |
| EVERY_N_SECONDS | 1 | 1 | Simple `uptime_ms()` check or `esp_timer` periodic | Pending |
| ~~millis/delay/micros~~ | ~~105~~ | — | `uptime_ms()`, `uptime_us()` in `main.h`; `vTaskDelay(pdMS_TO_TICKS(ms))`; `esp_timer_get_time()` | **Done** |
| ~~SPI (SPIClass)~~ | ~~56~~ | — | Raw GPSPI2/GPSPI3 register access (`soc/spi_struct.h`). PSRAM uses hardware cmd/addr phases; LoRa uses custom `EspHal` RadioLib HAL in `lora/lora_hal.h`. | **Done** |
| ~~GPIO (pinMode/digitalWrite)~~ | ~~40~~ | — | `driver/gpio.h`, `gpio_set_level()`, `gpio_set_direction()` | **Done** |
| ~~WebServer~~ | ~~37~~ | — | `esp_http_server.h` (IDF built-in). URI-matching handlers with `httpd_req_t*`, runs in own FreeRTOS task (no polling). OTA uses raw binary POST via JavaScript `fetch()`. | **Done** |
| ~~String class~~ | ~~26~~ | — | `snprintf()` + static char buffers with `page_cat`/`page_catf` helpers | **Done** |
| ~~ESP.* helpers~~ | ~~13~~ | — | `esp_chip_info()`, `esp_get_free_heap_size()`, `heap_caps_get_total_size()`, `spi_flash_get_chip_size()`, `esp_clk_cpu_freq()`, `esp_spiram_get_size()`, `esp_restart()` | **Done** |
| ~~Wire (I2C)~~ | ~~10~~ | — | `driver/i2c.h` — custom MPU6500 driver replaces MPU9250_WE library | **Done** |
| ~~Update (OTA)~~ | ~~8~~ | — | `esp_ota_ops.h` + `esp_partition.h` (inline handlers in http.cpp) | **Done** |

**Migration difficulty by subsystem:**

| Subsystem | Difficulty | Status |
|-----------|------------|--------|
| String class | Easy | **Done** — static buffers with `page_cat`/`page_catf` in http.cpp, `cfg_cat`/`cfg_catf` in config.cpp, `snprintf` in lut.cpp, C string ops in shell.cpp, byte-array `readData` in lora.cpp |
| ESP.* helpers | Easy | **Done** — direct IDF API calls |
| GPIO, timing | Easy | **Done** — `driver/gpio.h` for GPIO; `uptime_ms()`/`uptime_us()` in `main.h` for timing; `vTaskDelay(pdMS_TO_TICKS())` for delay |
| I2C (Wire) | Easy | **Done** — custom MPU6500 driver in `sensors/mpu6500.cpp` using `driver/i2c.h`, replacing MPU9250_WE library |
| OTA (Update) | Easy | **Done** — `esp_ota_ops.h` + `esp_partition.h`, inline handlers in http.cpp |
| SPI (PSRAM/LoRa) | Medium | **Done** — PSRAM uses raw GPSPI2 registers with hardware cmd/addr/dummy phases; LoRa uses custom `EspHal` RadioLib HAL with raw GPSPI3 registers (`lora/lora_hal.h`). See PSRAM section for performance analysis. |
| WebServer | Hard | **Done** — `esp_http_server` with URI handlers, raw binary OTA upload, JS-driven form |
| LittleFS | Easy | Pending — already an IDF component; swap `File` class for POSIX calls |
| WiFi | Medium | Pending — thin wrapper over `esp_wifi.h`, but event model differs |
| Serial/HWCDC | Medium | Pending — core debug path, must preserve core-1 pinning. Need `tinyusb` CDC or UART driver. |

**In progress.** Three subsystems remain: Serial/HWCDC (~135 call sites), LittleFS (~77),
and WiFi (~39). All remaining replacements are mechanical (no architectural changes needed).

## Architecture

### Dual-Core Threading Model

FreeRTOS on ESP32-S3 uses **preemptive scheduling with time slicing** (`configUSE_PREEMPTION=1`, `configUSE_TIME_SLICING=1`, 1000 Hz tick). Equal-priority tasks on the same core are time-sliced at 1ms intervals. Higher-priority tasks preempt immediately.

| Task | Core | Priority | Stack | Source | Lifecycle |
|------|------|----------|-------|--------|-----------|
| loopTask | 1 | 1 | 8192 | Arduino `loop()` in `main.cpp` | Always running |
| ShellTask | 1 | 1 | 8192 | `shell_task_fun` in `main.cpp` | Always running |
| httpd | 1 | 6 | 6144 | `esp_http_server` in `http/http.cpp` | Always running |
| led_render | 1 | 2 | 4096 | `led_task_fun` in `led/led.cpp` | Always running |
| BasicTask | any | 1 | 16384 | `basic_task_fun` in `basic/basic_wrapper.cpp` | Created on first script |
| WasmTask | 1 | 1 | 16384 | `wasm_task_fun` in `wasm/wasm_wrapper.cpp` | Created on first script |

**Core 1 tasks:**
- **loopTask** — Hardware polling: LoRa RX, GPS parsing, sensor polling, WiFi, NTP, cue engine, LED heartbeat blink. All non-blocking polling, yields via `vTaskDelay(1)` each iteration. (`http_loop()` is called but is a no-op — HTTP is handled by the httpd task.)
- **httpd** — ESP-IDF `esp_http_server` task. Handles all HTTP requests autonomously (no polling needed). Pinned to core 1 for HWCDC safety (handlers call `printfnl()`). Stack 6144 for HTML generation and OTA streaming.
- **ShellTask** — CLI input processing (`prepInput`), command execution, interactive apps (editor, game). Yields via `vTaskDelay(1)` each iteration. Blocking commands (editor, game) run here without blocking loopTask.
- **led_render** — Calls `FastLED.show()` at ~30 FPS when dirty, at least 1/sec unconditionally. Priority 2 preempts both loopTask and ShellTask.

**Script tasks (created on first use, not at boot):**
- **BasicTask** — BASIC interpreter. `tskNO_AFFINITY` — scheduler places on whichever core has bandwidth (typically core 0 since core 1 is busier).
- **WasmTask** — WASM interpreter. Pinned to core 1 for HWCDC safety (prints status/error messages via `printfnl`).

**Critical rules:**
- After `setup()`, only `led_render` calls `FastLED.show()`. All other code writes to `leds1`-`leds4` and calls `led_show()` to set the dirty flag. During `setup()` only, `led_show_now()` may be used.
- `vTaskDelay()` calls in task loops are for CPU efficiency (avoid busy spin), not for yielding — FreeRTOS preempts at 1ms ticks regardless. Do NOT add yields inside `editor_draw()` or similar atomic screen-update functions; it would cause visible screen tearing.

### USB Serial (HWCDC) Thread Safety

**The ESP32-S3 USB CDC (HWCDC) has a known bug that causes data corruption when accessed from multiple cores.** This is a hardware/driver-level race condition in the HWCDC TX FIFO, documented in multiple open issues against the Espressif Arduino core ([#9378](https://github.com/espressif/arduino-esp32/issues/9378), [#10836](https://github.com/espressif/arduino-esp32/issues/10836), [#11959](https://github.com/espressif/arduino-esp32/issues/11959)). Espressif's official USB Serial/JTAG documentation does not mention this limitation.

**Root cause:** The HWCDC has a 64-byte TX FIFO drained by a USB interrupt handler. `Serial.begin()` in `setup()` binds that interrupt to the calling core (core 1 on Arduino). When task code calls `Serial.write()` from a different core, the write and the interrupt handler access the FIFO registers simultaneously — a true hardware race that no software mutex can prevent, because the interrupt doesn't respect FreeRTOS mutexes. Symptoms: garbled output, lost bytes, interleaved fragments from different `print()` calls.

**The fix:** Pin all tasks that write to Serial to core 1, the same core as the HWCDC interrupt. On a single core, the interrupt *preempts* the task (cleanly saving/restoring state) rather than *racing* it on a separate core. This is the model the FIFO hardware was designed for.

**Mandatory constraints:**
1. **All tasks that write to Serial MUST be pinned to core 1** (`xTaskCreatePinnedToCore(..., 1)`) — same core as the HWCDC interrupt handler. Currently this includes ShellTask, WasmTask, loopTask, and led_render. BasicTask does not write to Serial directly (output goes through the BASIC runtime's print mechanism on ShellTask).
2. **All Serial output after `setup()` must go through `printfnl()`** which holds `print_mutex`. Direct `Serial.print()` calls from any task will bypass the mutex and corrupt output.
3. **DualStream and TelnetServer both override `write(const uint8_t*, size_t)`** for efficient bulk writes. The per-byte `write(uint8_t)` override still exists for compatibility.
4. **ESP-IDF component logging is suppressed** via `esp_log_level_set("*", ESP_LOG_NONE)` before ShellTask creation. With `ARDUINO_USB_CDC_ON_BOOT=1`, ESP-IDF log output shares the same USB CDC and bypasses `print_mutex`.
5. **Interactive apps** (editor, game) use `setInteractive(true)` which makes `printfnl()` return immediately without writing. The app then writes directly to the stream under `getLock()`/`releaseLock()`. This is safe because ShellTask is the only task writing to Serial at that point.

**TODO — core 1 bottleneck:** Pinning every Serial-writing task to core 1 is the simplest HWCDC workaround but leaves core 0 underutilized while core 1 is crowded (loopTask + ShellTask + WasmTask + led_render). A better long-term solution would decouple Serial output from the writing task — e.g., a FreeRTOS StreamBuffer where any task on any core enqueues text, and a small dedicated core 1 task drains it to Serial. This would let WasmTask and BasicTask run on either core freely while keeping all actual HWCDC FIFO writes on core 1.

**If you add a new FreeRTOS task that needs Serial output:** Pin it to core 1 and route output through `printfnl()`. Never call `Serial.print()` directly. Do not use `tskNO_AFFINITY` for tasks that produce Serial output — the HWCDC corruption bug makes cross-core writes unsafe regardless of mutexes.

### Thread Communication

- **printManager** (`console/printManager.cpp/h`): Mutex-protected logging. All text output outside of `setup()` must go through `printfnl()`. Each message has a `source_e` tag (SOURCE_BASIC, SOURCE_WASM, SOURCE_GPS, SOURCE_LORA, etc.) for filtering. The mutex also protects shell suspend/resume (erasing and redrawing the input line around background output). Tagged debug messages are sent to three sinks (payload built once, ANSI stripped once): (1) **PSRAM ring buffer** — always active, 256 entries × 300 bytes on ConeZ PCB / 32 entries on Heltec, viewable with `log` command; (2) **file sink** — optional, opened with `log to <path>`, appends to LittleFS file; (3) **MQTT** — published to `conez/{id}/debug` when connected. The ring buffer and file sinks include SOURCE_MQTT messages (useful for debugging MQTT); the MQTT sink excludes SOURCE_MQTT to prevent feedback loops. `SOURCE_COMMANDS_PROMPT` is a sink-only source (no console output) used to log CLI command prompts without echoing. Timestamps use decimal seconds format (`[100.123]`). `log_init()` is called in `setup()` after `psram_setup()` to allocate the ring buffer.
- **Shell** (`util/shell.cpp/h`): Command-line processor with cursor editing, history, and tab completion. Quote-aware tokenizer handles `"..."` grouping and `\"` / `\\` escapes. Commands are registered via `addCommand(name, func, fileSpec, subcommands, tabCompleteFunc, valArgs)`. See "Adding CLI Commands" section below for the complete guide.
- **Params** (`set_basic_param` / `get_basic_param`): 16-slot integer array for passing values between main loop and scripting runtimes. Accessed via `GETPARAM(id)` in BASIC or `get_param(id)`/`set_param(id,val)` in WASM.
- **Script loading** (`set_script_program`): Auto-detects `.bas` vs `.wasm` by extension, routes to the appropriate runtime's mutex-protected queue. Creates the interpreter task on first use (lazy initialization).

### Adding CLI Commands

All CLI commands live in `firmware/src/console/commands.cpp`. The shell is in `firmware/src/util/shell.h` / `shell.cpp`.

#### Step 1: Write the command function

```cpp
// Signature: int func(int argc, char **argv)
// Return 0 for success, non-zero for error.
// argv[0] is the command name, argv[1..] are arguments.
// Use printfnl(SOURCE_COMMANDS, F("..."), ...) for output.
int cmd_example(int argc, char **argv)
{
    if (argc < 2) {
        printfnl(SOURCE_COMMANDS, F("Usage: example <arg>\n"));
        return 1;
    }
    printfnl(SOURCE_COMMANDS, F("Got: %s\n"), argv[1]);
    return 0;
}
```

#### Step 2: Register with addCommand()

In `init_commands()` at the bottom of `commands.cpp`, call `shell.addCommand()`. Commands are inserted in alphabetical order automatically.

```cpp
void addCommand(const __FlashStringHelper *name, CommandFunction f,
                const char *fileSpec = NULL,
                const char * const *subcommands = NULL,
                TabCompleteFunc tabCompleteFunc = NULL,
                bool valArgs = false);
```

**Parameters:**

| # | Parameter | Type | Purpose | Default |
|---|---|---|---|---|
| 1 | `name` | `F("...")` | Command name + optional help text after a space. The part before the first space is the command name. | required |
| 2 | `f` | function ptr | `int func(int argc, char **argv)` | required |
| 3 | `fileSpec` | `const char *` | Filename completion pattern. `NULL` = no files, `"*"` = all files, `"*.bas;*.c"` = extension filter (semicolon-separated), `"/"` = directories only. A leading `/` is auto-inserted for the user. | `NULL` |
| 4 | `subcommands` | `const char * const *` | NULL-terminated static array for first-argument completion. Ignored when `tabCompleteFunc` is set. | `NULL` |
| 5 | `tabCompleteFunc` | `TabCompleteFunc` | Callback for multi-level completion (see below). When set, handles all argument positions (subcommands param is ignored). | `NULL` |
| 6 | `valArgs` | `bool` | If true, show `<val>` indicator at word positions 2+ for commands where subcommands universally take typed values. Only used when there's no callback. | `false` |

**Registration examples:**

```cpp
// No completion — just shows <cr> after the command
shell.addCommand(F("reboot"), cmd_reboot);

// File completion — all files
shell.addCommand(F("cat"), listFile, "*");

// File completion — filtered by extension
shell.addCommand(F("compile"), cmd_compile, "*.bas;*.c");

// File completion — directories only
shell.addCommand(F("dir"), listDir, "/");

// Static subcommands — completes first arg from list
shell.addCommand(F("color"), cmd_color, NULL, subs_color);

// Callback — multi-level typed completion
shell.addCommand(F("config"), cmd_config, NULL, NULL, tc_config);

// Alias — same function, same completion
shell.addCommand(F("radio"), cmd_lora, NULL, NULL, tc_lora);
```

#### Step 3: Add tab completion (if needed)

**Option A: No completion needed.** Commands with no arguments (e.g. `reboot`, `ps`, `sensors`) just register with the function pointer. TAB shows `<cr>`.

**Option B: File completion.** Set `fileSpec` (3rd arg). Use `"*"` for all files, `"*.ext1;*.ext2"` for filtered, `"/"` for directories only.

**Option C: Static subcommands.** Define a NULL-terminated `const char * const[]` array and pass as 4th arg. This only completes the first argument position.

```cpp
static const char * const subs_color[] = { "on", "off", NULL };
shell.addCommand(F("color"), cmd_color, NULL, subs_color);
```

**Option D: TabCompleteFunc callback.** For multi-level completion with type-specific hints. This is the most powerful option and should be used for any command with subcommands that take typed arguments.

```cpp
// Callback signature:
typedef const char * const * (*TabCompleteFunc)(
    int wordIndex,        // word being completed (1 = first arg, 2 = second, ...)
    const char **words,   // words[0..nWords-1], words[0] = command name
    int nWords            // number of complete words before cursor
);

// Return values:
//   Static array (NULL-terminated) — complete from this list
//   TAB_COMPLETE_FILES             — complete filenames from LittleFS
//   TAB_COMPLETE_VALUE             — show <val> (generic value expected)
//   TAB_COMPLETE_VALUE_STR         — show <string>
//   TAB_COMPLETE_VALUE_INT         — show <int>
//   TAB_COMPLETE_VALUE_FLOAT       — show <float>
//   TAB_COMPLETE_VALUE_HEX         — show <hex>
//   NULL                           — no completion (shows <cr>)
```

**Callback pattern** — dispatch on `wordIndex` and previously typed words:

```cpp
static const char * const * tc_example(int wordIndex, const char **words, int nWords) {
    if (wordIndex == 1) return subs_example;           // first arg: subcommand list
    if (wordIndex == 2 && nWords >= 2) {
        if (strcasecmp(words[1], "name") == 0) return TAB_COMPLETE_VALUE_STR;
        if (strcasecmp(words[1], "count") == 0) return TAB_COMPLETE_VALUE_INT;
        if (strcasecmp(words[1], "freq") == 0) return TAB_COMPLETE_VALUE_FLOAT;
        if (strcasecmp(words[1], "color") == 0) return TAB_COMPLETE_VALUE_HEX;
        if (strcasecmp(words[1], "mode") == 0) return subs_modes;  // enum-like
        if (strcasecmp(words[1], "load") == 0) return TAB_COMPLETE_FILES;
        // "status" takes no further args → return NULL → shows <cr>
    }
    return NULL;
}
```

**Existing callbacks** (in `commands.cpp`, use as reference):

| Callback | Command | Levels |
|---|---|---|
| `tc_debug` | `debug` | word 1: sources, word 2: on/off |
| `tc_config` | `config` | word 1: set/unset/reset, word 2: sections then keys, word 3: type-specific value hint (or on/off for bools) |
| `tc_cue` | `cue` | word 1: load/start/stop/status, word 2: files (load) or `<int>` (start) |
| `tc_wifi` | `wifi` | word 1: enable/disable/ssid/password, word 2: `<string>` for ssid/password |
| `tc_gpio` | `gpio` | word 1: set/out/in/read, word 2: `<int>` (pin), word 3: `<int>` (value) or up/down/none (pull) |
| `tc_lora` | `lora`/`radio` | word 1: freq/power/bw/sf/cr/mode/save/restart/send, word 2: typed hint or lora/fsk enum |
| `tc_mqtt` | `mqtt` | word 1: broker/port/enable/.../pub, word 2-3: typed hints |
| `tc_gps` | `gps` | word 1: info/set/save/restart/send, word 2: set→baud/rate/mode/nmea, restart→hot/warm/cold/factory, word 3: typed hints or constellation enum |
| `tc_led` | `led` | word 1: set/clear/count, word 2-4: typed hints per subcommand |
| `tc_psram` | `psram` | word 1: test/freq/cache, word 2: forever (test) or `<int>` (freq) |
| `tc_param` | `param` | word 1: `<int>` (index), word 2: `<int>` (value) |

#### Step 4: Add subcommand arrays

Subcommand arrays are static `const char * const[]` in `commands.cpp`, stored in `.rodata`. Define them near other `subs_*` arrays (around line 2862). They must be NULL-terminated.

```cpp
static const char * const subs_example[] = { "start", "stop", "status", NULL };
```

**Ordering rule:** Callback functions must be defined after the subcommand arrays they reference. The existing code groups all `subs_*` arrays first, then all `tc_*` callbacks, then `init_commands()`.

#### Step 5: Add help text

The `F("name ...")` string in `addCommand` doubles as help text. Everything after the first space is shown by the `help` command. Keep it concise — the help display is a single-column list.

#### Step 6: Add documentation

1. **`documentation/cli-commands.txt`** — add a quick reference line and a detailed description section
2. **`AGENTS.md`** — if the command interacts with a subsystem already documented, update that section

#### Step 7: Add aliases (optional)

Aliases are just additional `addCommand` calls pointing to the same function with the same completion:

```cpp
shell.addCommand(F("lora"), cmd_lora, NULL, NULL, tc_lora);
shell.addCommand(F("radio"), cmd_lora, NULL, NULL, tc_lora);
```

#### Conventions and rules

- **Output:** Use `printfnl(SOURCE_COMMANDS, F("...\n"), ...)` for all output. The `F()` macro keeps strings in flash. Always end format strings with `\n`.
- **Paths:** Call `normalize_path(buf, sizeof(buf), argv[1])` for any filename argument — prepends `/` if missing.
- **Subcommand dispatch:** Use `strcasecmp()` for case-insensitive matching. Check `argc` before accessing `argv[N]`.
- **Integer arguments:** Use `parse_int(str)` (alias for `strtol(s, NULL, 0)`) instead of `atoi()`. This handles `0x` hex, `0` octal, and plain decimal. Defined at the top of `commands.cpp`.
- **String arguments with spaces:** The shell tokenizer handles `"..."` quoting and `\"` / `\\` escapes automatically. Commands receive clean, unquoted strings in `argv[]` — no manual reassembly needed. Don't rejoin `argv[3..]` with spaces; instead, require users to quote strings with spaces (e.g. `wifi ssid "My Network"`).
- **Config values:** Commands that change settings usually hot-apply but don't save to config. Tell users to use `config set` to persist. Exception: `config set` itself saves immediately.
- **Board guards:** Wrap hardware-specific code in `#ifdef BOARD_HAS_*` with a fallback message.
- **Stack budget:** ShellTask is 8KB. Avoid large stack arrays (>1KB). Heap-allocate instead.
- **Error return:** Return 0 for success, non-zero for errors. The shell stores the last return code.
- **Print mutex:** `printfnl()` handles locking. For direct stream writes, use `getLock()`/`releaseLock()` around the output block.
- **Help consistency:** Format subcommands in the help string to match existing commands.

### BASIC Interpreter Extensions

The BASIC interpreter (third-party, by Jerry Williams Jr, in `basic/basic.h`) is extended via callback-based hooks in `basic/basic_extensions.h`. Hardware access is abstracted through registered callback functions:

- `register_location_callback` — GPS data (origin lat/lon, current position, speed, direction)
- `register_imu_callback` — MPU6500 roll/pitch/yaw/acceleration
- `register_datetime_callback` — Date/time from GPS
- `register_sync_callback` — Event waiting (GPS PPS, timers, params)
- `register_env_callback` — Temperature, humidity, brightness

New BASIC functions are added by: (1) defining the C function that manipulates the stack (`*sp`), (2) adding a token `#define`, (3) registering it in `funhook_()` with argument count validation.

### WASM Runtime

WebAssembly interpreter via wasm3 in `wasm/`. Guarded by `INCLUDE_WASM` build flag. Loads `.wasm` binaries from LittleFS and runs them on WasmTask (core 1). Entry point conventions: `setup()` + `loop()` (Arduino-style, loop runs until stopped), or `_start()` / `main()` (single-shot).

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
| `wasm_imports_compression.cpp` | inflate_file, inflate_file_to_mem, inflate_mem (gzip/zlib/raw deflate) |
| `wasm_imports_deflate.cpp` | deflate_file, deflate_mem_to_file, deflate_mem (gzip compression) |

Each file contains its wrapper functions and a `link_*_imports()` function that registers them. Adding a new host import is a single-file edit: add the `m3ApiRawFunction` wrapper and a `m3_LinkRawFunction` call in the same file's link function.

**Naming convention:** Host imports that are useful to C and Rust modules use short names matching libc conventions (e.g. `sinf`, `get_temp`, `led_fill`). Imports that exist only to support the BASIC-to-WASM compiler and aren't useful for C/Rust modules use a `basic_` prefix (e.g. `basic_str_alloc`, `basic_str_concat`, `basic_str_hex`).

**Host imports** (module `"env"`): LED (`led_set_pixel`, `led_fill`, `led_show`, `led_count`, `led_set_pixel_hsv`, `led_fill_hsv`, `hsv_to_rgb`, `rgb_to_hsv`, `led_gamma8`, `led_set_gamma`, `led_set_buffer`, `led_shift`, `led_rotate`, `led_reverse`), GPIO (`pin_set`, `pin_clear`, `pin_read`, `analog_read`), GPS (`get_lat`, `get_lon`, `get_alt`, `get_speed`, `get_dir`, `gps_valid`), GPS origin/geometry (`get_origin_lat`, `get_origin_lon`, `has_origin`, `origin_dist`, `origin_bearing`), IMU (`get_roll`, `get_pitch`, `get_yaw`, `get_acc_x/y/z`, `imu_valid`), environment (`get_temp`, `get_humidity`, `get_brightness`, `get_bat_voltage`, `get_solar_voltage`), sun position (`get_sunrise`, `get_sunset`, `sun_valid`, `is_daylight`), cue engine (`cue_playing`, `cue_elapsed`), date/time (`get_year`, `get_month`, `get_day`, `get_hour`, `get_minute`, `get_second`, `get_day_of_week`, `get_day_of_year`, `get_is_leap_year`, `time_valid`, `get_epoch_ms`, `millis`, `millis64`, `delay_ms`), event sync (`wait_pps`, `wait_param`), params (`get_param`, `set_param`, `should_stop`), output (`print_i32`, `print_f32`, `print_i64`, `print_f64`, `print_str`), math float (`sinf`, `cosf`, `tanf`, `asinf`, `acosf`, `atanf`, `atan2f`, `powf`, `expf`, `logf`, `log2f`, `fmodf`), math double (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `pow`, `exp`, `log`, `log2`, `fmod`), curve (`lerp`, `larp`, `larpf`), printf/scanf (`host_printf`, `host_snprintf`, `host_sscanf`), LUT (`lut_load`, `lut_get`, `lut_size`, `lut_set`, `lut_save`, `lut_check`), file I/O (`file_open`, `file_close`, `file_read`, `file_write`, `file_size`, `file_seek`, `file_tell`, `file_exists`, `file_delete`, `file_rename`), compression (`inflate_file`, `inflate_file_to_mem`, `inflate_mem`, `deflate_file`, `deflate_mem_to_file`, `deflate_mem`). Full reference in `documentation/wasm-api.txt`.

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
  -Wl,--allow-undefined -Wl,-z,stack-size=256 \
  -I . -o module.wasm module.c
llvm-strip module.wasm
```

**Authoring modules (Rust):** Two approaches:
- **`#![no_std]`** targeting `wasm32-unknown-unknown` — minimal binary (~350 bytes). Declare ConeZ host imports as `extern "C"`, use `#[no_mangle]` exports. See `tools/wasm/examples/rust_rgb_cycle/` for a complete example.
- **std** targeting `wasm32-wasip1` — `println!()` works via WASI `fd_write` but adds ~35KB of formatting overhead. See `tools/wasm/examples/rust_rainbow/`.

Both use `cdylib` crate type, `opt-level = "z"`, LTO, strip, `panic = "abort"`. Post-process with `wasm-opt --enable-bulk-memory -Oz` to reduce binary size.

See `tools/wasm/examples/` for sample modules (C and Rust). Use `make -C tools/wasm` to build all examples and `make -C tools/wasm install` to copy them to `firmware/data/` for LittleFS upload.

**Offline compiler tools:** `tools/bas2wasm/` (BASIC→WASM) and `tools/c2wasm/` (C→WASM) are self-contained compilers with no external dependencies beyond libc. Each has its own `buildnum.txt` for independent version tracking. Build each with `make` from its directory, or `make` from `tools/` to build all. Both compilers also work as embedded libraries — the simulator and firmware link them directly (no subprocess spawning) via platform abstraction headers (`bas2wasm_platform.h`, `c2wasm_platform.h`). In embedded mode, `malloc`/`fprintf`/`exit` redirect to host-provided callbacks, and symbol names are prefixed (`bw_`/`cw_`) to avoid link-time collisions when both compilers coexist in the same binary. See `documentation/bas2wasm.txt` and `documentation/c2wasm.txt` for full references.

### Desktop Simulator

Qt6-based simulator in `simulator/conez/` that runs WASM programs on a Linux desktop without hardware. Provides an LED visualizer (4 channels, 30 FPS), interactive sensor sliders, and a console. Uses the same vendored wasm3 interpreter and the same 157 host imports as the firmware, so programs that work in the simulator work on hardware.

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

**Data directory:** `simulator/conez/data/` ships example scripts copied from `firmware/data/`. Auto-detected at startup relative to the binary; overridable with `--sandbox`. CLI commands (`dir`, `cat`, `del`, `ren`, `cp`, `mkdir`, `rmdir`, `grep`, `hexdump`, `df`) and WASM file I/O operate in this directory. Bare filenames in `run` resolve here.

**Console commands:** `?`/`help`, `run`, `stop`, `open`, `dir`/`ls`, `del`/`rm`, `cat`/`list`, `ren`/`mv`, `cp`, `mkdir`, `rmdir`, `grep`, `hexdump`, `df`, `clear`/`cls`, `param`, `led`, `sensors`, `time`/`date`, `uptime`, `ver`/`version`, `wasm`, `cue`, `mqtt`, `inflate`/`gunzip`, `deflate`/`gzip`. These mirror the firmware CLI; hardware-only commands (art, color, config, debug, edit, game, gpio, gps, history, load, lora, mem, ps, psram, radio, reboot, tc, wifi, winamp) are not available.

**Source layout:** `src/gui/` (LED strip, console, sensor panel widgets), `src/state/` (LED buffers, sensor mock, config, cue engine), `src/wasm/` (runtime + 12 import files mirroring firmware), `src/worker/` (QThread for WASM, embedded compilation), `src/compiler/` (single-TU wrappers for embedded bas2wasm and c2wasm). Vendored wasm3 in `thirdparty/wasm3/source/`. Example data in `data/`.

**Threading:** Main thread runs Qt event loop and all widgets. WasmWorker QThread runs the wasm3 interpreter. Communication via Qt signals/slots with queued connections. Shared state (LedState, SensorState) is mutex-protected.

**Differences from firmware:** Sensors come from GUI sliders (not real hardware). GPIO stubs log to console. No LoRa, WiFi, or PSRAM. Cue engine plays back .cue files with full timeline support; when not playing, cue_playing/cue_elapsed fall back to sensor panel sliders. File I/O uses the data directory. DateTime uses host system clock.

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

**WASM API versioning:** When adding or changing host imports, bump `CONEZ_API_VERSION` in three places: `tools/wasm/conez_api.h`, `tools/bas2wasm/bas2wasm.h`, and `tools/c2wasm/c2wasm.h`. The compilers and runtimes display this version via `*_version_string()` functions (shown by the `ver` command in firmware and simulator). This lets users verify their compiler matches the firmware's API.

### Mayhem (Cue-List Editor)

Avalonia/C# desktop application in `c_sharp/mayhem/` for authoring timeline-based lighting choreography. Users arrange effects (color gradients, FX presets, BASIC/WASM scripts, audio/video media) on a multi-channel timeline, set timing cues, and export bundled cue-lists for deployment to hardware. Projects save as `.clf` (JSON).

```bash
# Linux (Debian/Ubuntu)
sudo apt install dotnet-sdk-10.0 ffmpeg libavcodec-dev libavformat-dev \
    libswscale-dev libswresample-dev libavutil-dev
cd c_sharp/mayhem
dotnet run --project Mayhem/Mayhem.csproj

# macOS (Homebrew)
brew install dotnet ffmpeg
cd c_sharp/mayhem
dotnet run --project Mayhem/Mayhem.csproj
```

**Dependencies:** .NET 10.0 SDK, Avalonia 11.3, FFmpeg libraries (audio/video decoding via FFmpeg.AutoGen bindings). FFmpeg library paths are auto-detected from standard system locations and from the `ffmpeg` binary on PATH. Set `FFMPEG_PATH` to override if auto-detection fails.

**Source layout:** `Models/` (Project, Channel, Effect types, Cue), `ViewModels/` (MVVM), `Services/` (FFmpeg decoding, audio playback, project file I/O), `Converters/` (XAML value converters). Sample scripts in `Scripts/`.

See `c_sharp/mayhem/readme.md` for quick-start instructions.

### Build Flags

`INCLUDE_BASIC` and `INCLUDE_WASM` in `platformio.ini` build_flags control which scripting runtimes are compiled in. Both are enabled by default. Remove either flag to exclude that runtime and save flash space (~67KB for wasm3).

`INCLUDE_BASIC_COMPILER` and `INCLUDE_C_COMPILER` control the embedded bas2wasm and c2wasm compilers (on-device .bas/.c → .wasm compilation). Both are enabled by default. The `compile` CLI command compiles `.bas` files to `.wasm` on the device; optionally auto-runs the result with `compile file.bas run`.

**Build size (conez-v0-1, ESP32-S3, 327,680 RAM / 2,097,152 flash, both compilers):**

| Version | RAM | RAM % | Flash | Flash % |
|---------|-----|-------|-------|---------|
| v0.01.x (Arduino framework) | 225,572 | 68.8% | 1,224,281 | 58.4% |
| v0.02.x (ESP-IDF + Arduino, `-Os`) | 127,260 | 38.8% | 1,105,237 | 52.7% |

Both compilers use dynamic allocation — large arrays are heap-allocated during compilation and freed afterward, so their permanent RAM cost is minimal (bas2wasm ~3.9KB, c2wasm ~5.8KB). Import tables are `const` (flash-mapped). On boards with PSRAM, bas2wasm's `data_buf` (4KB) and `data_items` (4KB) are allocated via `psram_malloc()` instead of the DRAM heap, reducing transient DRAM usage from ~26KB to ~18KB during compilation. Access goes through `bw_psram_read()`/`bw_psram_write()` (page-cache-friendly sequential patterns). Controlled by `BAS2WASM_USE_PSRAM`, auto-set in the firmware embed wrapper when `BOARD_HAS_IMPROVISED_PSRAM` or `BOARD_HAS_NATIVE_PSRAM` is defined. On boards without PSRAM (Heltec), `psram_malloc()` falls back to the system heap transparently.

### Board Abstraction

`board.h` defines board-specific hardware via compile-time `#ifdef`:
- `BOARD_CONEZ_V0_1` — Custom PCB, SX1268 LoRa, GPS, buzzer, 8MB aux SPI PSRAM (LY68L6400SLIT)
- `BOARD_HELTEC_LORA32_V3` — Heltec dev board, SX1262 LoRa, no GPS, no PSRAM

Pin assignments for the ConeZ PCB are in `board.h`. LED buffer pointers and setup are in `led/led.h`/`led.cpp`; per-channel LED counts are runtime-configurable via the `[led]` config section. The board is selected via build flags in `platformio.ini`.

### Key Hardware Interfaces

- **LoRa:** RadioLib, SX1262/SX1268 via SPI (custom `EspHal` HAL in `lora/lora_hal.h` — raw GPSPI3 registers, ESP-IDF GPIO/timers, no Arduino dependency). Two modes selectable via `lora.rf_mode` config key. **LoRa mode** (default): configurable frequency/BW/SF/CR (defaults: 431.250 MHz, SF9, 500 kHz BW). **FSK mode**: configurable bit rate, frequency deviation, RX bandwidth, data shaping, whitening, sync word (hex string), CRC. Shared params (frequency, TX power, preamble) work in both modes. CLI `lora` subcommands (`freq`, `power`, `bw`, `sf`, `cr`, `mode`) hot-apply changes without saving to config; `config set lora.*` persists but requires reboot.
- **GPS:** ATGM336H (AT6558 chipset) via UART (9600 baud), parsed by inline NMEA parser (`sensors/nmea.h`/`nmea.cpp` — pure C, no Arduino dependency), with PPS pin for interrupt-driven timing (see Time System below). TX pin wired for PCAS configuration commands (`gps_send_nmea()` in `sensors/gps.cpp`). Parser handles RMC, GGA, and GSA sentences from any GNSS talker ID.
- **LEDs:** FastLED WS2811 on 4 GPIO pins, BRG color order. Per-channel LED counts are configurable via `[led]` config section (default: 50 each). Buffers are dynamically allocated at boot. Default boot color per channel configurable via `led.color1`–`color4` (hex 0xRRGGBB, default 0x000000/off). CLI `led count <ch> <n>` hot-resizes a channel (0 to disable) without saving to config; `config set led.countN` persists but requires reboot. Resize is mutex-protected against the render task. All FastLED interaction is centralized in `led/led.cpp`/`led.h`.
- **IMU:** MPU6500 on I2C 0x68 (custom driver in `sensors/mpu6500.cpp`/`.h` using `driver/i2c.h`)
- **Temp:** TMP102 on I2C 0x48 (inline driver in `sensors/sensors.cpp` — reads register 0x00 via Wire)
- **PSRAM:** 8MB external SPI PSRAM on ConeZ PCB. See PSRAM Subsystem section below.
- **WiFi:** STA mode, SSID/password from config system. CLI `wifi ssid`/`wifi psk` hot-apply (disconnect + reconnect) without saving to config; `config set wifi.*` persists but requires reboot.
- **HTTP/OTA:** ESP-IDF `esp_http_server` on port 80 (`http/http.cpp`). Runs in its own FreeRTOS task (pinned to core 1, stack 6144) — no polling needed. Root page shows GPS, partition info, and links to `/config`, `/dir`, `/nvs`, `/update`, `/reboot`. Config form submits URL-encoded POST body, parsed with `httpd_query_key_value()` + `url_decode()`. OTA firmware/filesystem upload at `/update` — HTML form uses JavaScript `fetch()` for raw binary POST (no multipart parsing), `type` passed as query param. CLI-friendly via curl:
  ```
  curl -X POST --data-binary @firmware.bin http://<ip>/update?type=firmware
  curl -X POST --data-binary @littlefs.bin http://<ip>/update?type=filesystem
  ```
  Uses Arduino `Update` library (wraps ESP-IDF `esp_ota_*`). Filesystem upload calls `LittleFS.end()` before writing. Auto-reboots on success. Progress logged via `printfnl(SOURCE_SYSTEM, ...)`.
- **MQTT:** Minimal MQTT 3.1.1 client in `mqtt/conez_mqtt.cpp/h`. Connects to the sewerpipe broker over WiFi using Arduino `WiFiClient`. State machine: DISCONNECTED → WAIT_CONNACK → CONNECTED. Auto-reconnects with exponential backoff (1s → 30s cap). Publishes JSON heartbeats every 30s to `conez/{id}/status` with uptime, heap, temp, RSSI. Debug messages are forwarded to `conez/{id}/debug` by printManager (SOURCE_MQTT excluded to prevent loops). Subscribes to `conez/{id}/cmd/#` for per-cone commands. PINGREQ at keepalive/2 (30s). Config section `[mqtt]` with `broker` (default: `sewerpipe.local`), `port` (default: 1883), and `enabled` (default: on) keys. CLI: `mqtt` (status), `mqtt enable`/`disable`, `mqtt connect`/`disconnect`, `mqtt broker <host>` (hot-apply), `mqtt pub <topic> <payload>`. Debug output via `SOURCE_MQTT` (default on). Runs on loopTask (core 1); ShellTask can safely call `mqtt_publish()` and force flags (same core, time-sliced). See `documentation/mqtt.txt` for topic hierarchy and protocol details.
- **CLI:** ConezShell (`util/shell.cpp/h`) on DualStream — both USB Serial and Telnet (port 23) active simultaneously, all output to both. TelnetServer (`console/telnet.cpp/h`) supports up to 3 simultaneous clients with per-slot IAC state, bare `\n` → `\r\n` translation on output, prompt delivery on connect, and Ctrl+D per-session disconnect. Arrow keys, Home/End/Delete, Ctrl-A/E/U, 32-entry command history (PSRAM-backed ring buffer on ConeZ PCB, single-entry DRAM fallback on Heltec). ANSI color output on by default, toggleable at runtime via `color on`/`color off` CLI command (`setAnsiEnabled()`/`getAnsiEnabled()` in `printManager.h`). Commands requiring ANSI (art, clear, game, winamp) error out when color is off; editor falls back to a line-based mode. `CORE_DEBUG_LEVEL=0` in `platformio.ini` suppresses Arduino library log macros (`log_e`/`log_w`/etc.) at compile time — these bypass `esp_log_level_set()` and would corrupt HWCDC output. File commands auto-normalize paths (prepend `/` if missing) via `normalize_path()` in `main.h`. Full-screen text editor (`console/editor.cpp/h`) for on-device script editing, with line-editor fallback when ANSI is disabled. See `documentation/cli-commands.txt` for the full command reference.

### Time System

Unified time API in `sensors/gps.h`/`gps.cpp` provides millisecond-precision epoch time across all board types. Consumers call `get_epoch_ms()` regardless of source.

**Tiered sources (higher priority wins):**
- **GPS + PPS** (time_source=2, ~1us accuracy) — ConeZ PCB only. PPS rising edge triggers `IRAM_ATTR` ISR that captures `millis()`. NMEA sentence (arriving ~100-200ms later) provides absolute time for the preceding edge. Epoch stored under `portMUX_TYPE` spinlock (64-bit not atomic on 32-bit Xtensa).
- **NTP** (time_source=1, ~10-50ms accuracy) — Any board with WiFi. Uses ESP32 SNTP via `configTime()`. Auto-initializes in `ntp_loop()` when WiFi connects (no need to call `ntp_setup()` from WiFi commands). Re-syncs periodically (`config.ntp_interval`, default 3600s). NTP server configurable via `[system] ntp_server` config key.
- **Compile-time** (time_source=0) — Fallback seeded from `BUILD_EPOCH_S` (UTC epoch at build time, computed by PlatformIO) at boot via `time_seed_compile()`. Provides approximate time (~seconds drift per day) when no GPS or NTP is available. `get_time_valid()` returns true, `get_epoch_ms()` returns a reasonable value. Automatically overridden when NTP or GPS connects.

**GPS staleness fallback:** If PPS stops arriving for >10 seconds (GPS loss), `ntp_loop()` downgrades `time_source` to 0, allowing NTP to take over if WiFi is available. Compile-time seed remains valid as a last resort.

**API:** `get_epoch_ms()` (ms since Unix epoch), `get_time_valid()` (any source active), `get_time_source()` (0/1/2), `get_pps_flag()` (ISR edge flag, clear-on-read), `time_seed_compile()`, `ntp_setup()`, `ntp_loop()`.

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

**DRAM page cache:** Write-back LRU cache for improvised SPI PSRAM (no-op on native/stubs). Default 128 pages × 512 bytes = ~65KB DRAM. Configurable at compile time via `PSRAM_CACHE_PAGES` and `PSRAM_CACHE_PAGE_SIZE`. Set `PSRAM_CACHE_PAGES` to 0 to disable.

**Thread safety:** All public functions are protected by a recursive FreeRTOS mutex. Safe to call from any task. The memory test (`psram_test`) runs without the mutex and requires exclusive access — refuses to run if any allocations exist.

**CLI:** `psram` shows status (size, used/free, contiguous, alloc slots used/max, cache hit rate, SPI clock) plus a 64-character allocation map (`-` free, `+` partial, `*` full) and a cache page map (`-` empty, `C` clean, `D` dirty), both ANSI colored. `psram cache` shows detailed per-page metadata (address, dirty status, LRU age) and hit/miss stats. `psram test` runs a full memory test with throughput benchmark (frees/re-allocates shell history ring buffer for exclusive PSRAM access). `psram test forever` loops until error or keypress. `psram freq <MHz>` changes SPI clock at runtime (5–80 MHz) for benchmarking. `mem` also includes a PSRAM summary.

**Hardware details (ConeZ PCB):** LY68L6400SLIT (Lyontek), 64Mbit/8MB, 23-bit address, SPI-only wiring (no quad), FSPI bus (SPI2) on GPIO 5/4/6/7 (CE/MISO/SCK/MOSI). These are **GPIO matrix** routed (not IOMUX), so the documented reliable max is 26 MHz; IOMUX pins (GPIO 10–13) would allow 80 MHz. Default boot clock: 40 MHz (APB/2). Read command auto-selects: slow read `0x03` (no wait, max 33 MHz) vs fast read `0x0B` (8 wait cycles, max 133 MHz). 8µs tCEM max per CE# assertion — driver chunks transfers to stay within budget. 1KB page size. Datasheet: `hardware/datasheets/LY68L6400SLIT.pdf`.

**SPI driver architecture:** The PSRAM driver uses **raw GPSPI2 (SPI2/FSPI) register access** — no Arduino SPI library, no ESP-IDF SPI master driver. All SPI operations go through `soc/spi_struct.h` register writes. Key components in `psram.cpp`:

- `spi2_init()` — enables SPI2 peripheral clock gate, configures mode 0 / MSB-first, routes pins via GPIO matrix (`esp_rom_gpio_connect_out_signal/in_signal`)
- `spi_freq_to_clkdiv()` — computes ESP32-S3 clock divider register value (4-bit `clkdiv_pre` × 6-bit `clkcnt_n`)
- `spi2_transfer()` — single-byte SPI with `spi2_set_bitlen` caching and `spi2_phases_dirty` cleanup
- **Hardware command/address/dummy phases** — the SPI peripheral sends cmd+addr+dummy from dedicated registers (`user.usr_command`, `addr`, `user1.usr_dummy_cyclelen`), keeping the 64-byte FIFO exclusively for data. Data is copied directly between the caller's buffer and `data_buf[]` via `uint32_t*` word access (memcpy fallback for unaligned buffers). This eliminates all intermediate buffer copies.

**tCEM chunking:** The LY68L6400 has an 8µs max CE# assertion time. The driver computes `psram_read_chunk` and `psram_write_chunk` based on frequency, accounting for command/address overhead, and caps at 64 bytes (FIFO capacity). At 80 MHz, the data chunk is capped at 64 bytes even though tCEM would allow 76, because cmd/addr are handled outside the FIFO. This is actually more tCEM-safe than the previous approach, which split 80-byte combined buffers across two FIFO loads with a CPU gap between them.

**Runtime SPI clock changes (`psram freq`):** `psram_change_freq()` writes the SPI2 clock register directly via `GPSPI2.clock.val`, computes the divider with `spi_freq_to_clkdiv()`, invalidates the bitlen cache, and is safe under `psram_mutex`.

**SPI migration performance analysis.** Four strategies were benchmarked during the Arduino SPI → raw register migration. All numbers from `psram test` on ConeZ PCB v0.1 (8MB LY68L6400SLIT):

| Strategy | 40 MHz Write | 40 MHz Read | 80 MHz Write | 80 MHz Read | Notes |
|---|---|---|---|---|---|
| Arduino `SPIClass` (baseline) | 2,800 | 2,216 | 4,534 | 3,327 | Arduino NL (no-lock) variant with `_inTransaction=true` |
| 1. Raw registers + combined buffer | 2,150 | 1,608 | 3,252 | 2,254 | cmd+addr+data in one tx[] array, byte-by-byte FIFO packing |
| 2. + memcpy packing + bitlen cache | 2,334 | 1,828 | 3,670 | 2,662 | `memcpy` to `uint32_t buf[16]` for FIFO pack/unpack; skip `cmd.update` when `ms_dlen` unchanged |
| 3. + direct `uint32_t*` FIFO access | 2,395 | 1,922 | 3,837 | 2,852 | Aligned buffers bypass intermediate buf[16]; `__attribute__((aligned(4)))` on stack arrays |
| **4. Hardware cmd/addr phases (final)** | **2,407** | **1,999** | **4,214** | **3,338** | SPI peripheral handles cmd+addr+dummy from registers; FIFO is data-only; zero intermediate buffers |

Key lessons: (a) ESP32-S3 requires `cmd.update` before `cmd.usr` whenever `ms_dlen` changes — removing it causes PSRAM to return MF=0x00 KGD=0x00 even on fresh power-up. (b) `clk_gate.clk_en/mst_clk_active/mst_clk_sel` must be enabled before any SPI operation — ESP32-S3 specific, not needed on ESP32. (c) The biggest win came from eliminating intermediate buffer copies via hardware phases, not from micro-optimizing the FIFO packing loop. (d) At 80 MHz, hardware phases actually fix a tCEM compliance issue — the old two-FIFO-load approach pushed CE# time slightly past 8µs.

**Throughput benchmarks (ConeZ PCB v0.1, 8MB, `psram test`, hardware SPI phases):**

| Actual SPI Clock | Read KB/s | Write KB/s | Notes |
|---|---|---|---|
| 40.00 MHz | 1,999 | 2,407 | Default boot clock (APB/2) |
| 80.00 MHz | 3,338 | 4,214 | APB/1 — requires IOMUX pins for reliability |

### Configuration

INI-style config file (`/config.ini`) on LittleFS, loaded at boot. Descriptor-table-driven: `cfg_table[]` in `config.cpp` maps `{section, key, type, offset}` to `conez_config_t` struct fields. Covers WiFi, GPS origin, LoRa radio params (both LoRa and FSK modes), MQTT broker, NTP server, timezone, LED counts and default colors, debug defaults, callsign, and startup script. CLI commands: `config`, `config set`, `config unset`, `config reset`. See `documentation/config.txt` for full reference.

### Filesystem

LittleFS on 4MB flash partition. Stores BASIC scripts (`.bas`), WASM modules (`.wasm`), C source (`.c`), LUT data files (`LUT_N.csv`), binary cue files (`.cue`), and optionally `/config.ini`. At boot, `script_autoexec()` searches for `/startup.bas`, `/startup.c`, `/startup.wasm` in order (only candidates whose runtime/compiler is compiled in). A `.c` file is compiled to `.wasm` then run. Setting `system.startup_script` in config overrides auto-detection.

### Versioning

All components use the format **`MAJOR.MINOR.BUILD`** with 2-digit minor and 4-digit build number: `0.02.0001`. The format string is `"%d.%02d.%04d"`. Each component has its own `buildnum.txt` that auto-increments on build.

**Firmware version history:**
- **v0.01.x** — Arduino framework (pre-compiled SDK)
- **v0.02.x** — ESP-IDF + Arduino component (SDK built from source, `sdkconfig.defaults`)
- **v0.03.x** — (planned) Pure ESP-IDF, no Arduino component. See "Arduino Migration Path" section.


| Component | Version defines | buildnum.txt location |
|---|---|---|
| Firmware | `custom_prog_version` in `platformio.ini` | `firmware/buildnum.txt` |
| Simulator | `VERSION_MAJOR`/`VERSION_MINOR` in `CMakeLists.txt` | `simulator/conez/buildnum.txt` |
| bas2wasm | `BAS2WASM_VERSION_MAJOR`/`_MINOR` in `bas2wasm.h` | `tools/bas2wasm/buildnum.txt` |
| c2wasm | `C2WASM_VERSION_MAJOR`/`_MINOR` in `c2wasm.h` | `tools/c2wasm/buildnum.txt` |
| sewerpipe | `SEWERPIPE_VERSION_MAJOR`/`_MINOR` in `sewerpipe.h` | `tools/sewerpipe/buildnum.txt` |

Firmware versioning uses `patch_firmware_ver.py` post-build to embed version/timestamp into the firmware binary. Configured via `custom_prog_*` fields in `platformio.ini`.

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

### Sewerpipe (MQTT Broker)

Lightweight MQTT 3.1.1 broker in `tools/sewerpipe/` for coordinating cues and commands across cones over local WiFi. Runs on a laptop or Pi as the show coordinator — cones connect as MQTT clients. Single-threaded `poll()` event loop, POSIX only (Linux/macOS).

**Capabilities:** QoS 0 + QoS 1, retained messages, topic wildcards (`+` single-level, `#` multi-level), `$`-prefix topic filtering, duplicate client ID takeover, keep-alive timeout enforcement. **Not supported:** TLS, auth, will messages, persistent sessions, QoS 2, `$SYS` topics.

```bash
# Build
cd tools/sewerpipe && make

# Run
./sewerpipe -v -p 1883

# Run tests (requires Python 3)
make test
```

**CLI:** `sewerpipe [-p port] [-d] [-v] [-h]`. Default port 1883. `-d` forks to background (binds first so errors are visible). `-v` enables verbose per-packet logging.

**Source files:** `sewerpipe.h` (types, constants), `mqtt.c` (packet parsing/serialization), `broker.c` (client lifecycle, subscriptions, routing, retained store, QoS 1 inflight), `main.c` (event loop, CLI, signal handling). Tests in `test/` — 5 integration tests using raw MQTT packets via Python.

**Limits:** 128 clients, 32 subscriptions/client, 256 retained messages, 16 inflight QoS 1 messages/client, 64KB receive buffer/client. See `documentation/sewerpipe.txt` for broker protocol details and `documentation/mqtt.txt` for the ConeZ topic hierarchy.
