# AGENTS.md

This file provides guidance to AI coding agents working with code in this repository.

## Project Overview

ConeZ is an ESP32-S3 embedded system that powers networked LED light displays on traffic cones for fun and interesting playa events. It combines GPS positioning, LoRa radio communication, IMU sensors, and RGB LED control via a BASIC interpreter running on a dedicated FreeRTOS thread.

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

# Upload LittleFS filesystem (BASIC scripts in firmware/data/)
pio run -e conez-v0-1 --target uploadfs
```

There are standalone test projects under `tests/` (psram_test, thread_test) — each is a separate PlatformIO project built/flashed independently from its own directory.

## Architecture

### Dual-Core Threading Model

- **Core 1 (main loop):** Hardware drivers — LoRa RX, GPS parsing, sensor polling, WiFi/HTTP, CLI shell, "direct effects" (speed-of-sound sync). Runs in `loop()` in `main.cpp`.
- **Core 1 (LED render task):** Dedicated FreeRTOS task (`led_task_fun` in `led.cpp`, 4KB stack, priority 2) that calls `FastLED.show()` at ~30 FPS when the dirty flag is set, and at least once per second unconditionally. This is the **only** place `FastLED.show()` is called after `setup()`.
- **Core 0 (BASIC task):** Dedicated FreeRTOS task (`basic_task_fun` in `basic_wrapper.cpp`, 65KB stack) that runs user BASIC scripts.

**Critical rule:** After `setup()`, only the LED render task calls `FastLED.show()`. All other code (BASIC scripts, effects, etc.) writes to the global LED buffers (`leds1`-`leds4`) and calls `led_show()` to set the dirty flag. During `setup()` only, `led_show_now()` may be used for immediate display.

### Thread Communication

- **printManager** (`printManager.cpp/h`): Mutex-protected logging. All text output outside of `setup()` must go through `printfnl()`. Each message has a `source_e` tag (SOURCE_BASIC, SOURCE_GPS, SOURCE_LORA, etc.) for filtering.
- **BASIC params** (`set_basic_param` / `get_basic_param`): 16-slot integer array for passing values between main loop and BASIC scripts. Accessed via `GETPARAM(id)` in BASIC.
- **Program loading** (`set_basic_program`): Mutex-protected; queues a .bas filename for the BASIC task to pick up.

### BASIC Interpreter Extensions

The BASIC interpreter (third-party, by Jerry Williams Jr, in `basic.h`) is extended via callback-based hooks in `extensions.h`. Hardware access is abstracted through registered callback functions:

- `register_location_callback` — GPS data (origin lat/lon, current position, speed, direction)
- `register_imu_callback` — MPU6500 roll/pitch/yaw/acceleration
- `register_datetime_callback` — Date/time from GPS
- `register_sync_callback` — Event waiting (GPS PPS, timers, params)
- `register_env_callback` — Temperature, humidity, brightness

New BASIC functions are added by: (1) defining the C function that manipulates the stack (`*sp`), (2) adding a token `#define`, (3) registering it in `funhook_()` with argument count validation.

### Board Abstraction

`board.h` defines board-specific hardware via compile-time `#ifdef`:
- `BOARD_CONEZ_V0_1` — Custom PCB, SX1268 LoRa, GPS, buzzer, 2MB aux PSRAM
- `BOARD_HELTEC_LORA32_V3` — Heltec dev board, SX1262 LoRa, no GPS, 8MB native PSRAM

Pin assignments for the ConeZ PCB are in `board.h`. LED buffer pointers and setup are in `led.h`/`led.cpp`; per-channel LED counts are runtime-configurable via the `[led]` config section. The board is selected via build flags in `platformio.ini`.

### Key Hardware Interfaces

- **LoRa:** RadioLib, SX1262/SX1268 via SPI, configurable frequency/BW/SF/CR (defaults: 431.250 MHz, SF9, 500 kHz BW)
- **GPS:** TinyGPSPlus on UART (9600 baud), with PPS pin for sync
- **LEDs:** FastLED WS2811 on 4 GPIO pins, BRG color order. Per-channel LED counts are configurable via `[led]` config section (default: 50 each). Buffers are dynamically allocated at boot. All FastLED interaction is centralized in `led.cpp`/`led.h`.
- **IMU:** MPU6500 on I2C 0x68 (custom driver in `lib/MPU9250_WE/`)
- **Temp:** TMP102 on I2C 0x48
- **WiFi:** STA mode, SSID/password from config system, with ElegantOTA at `/update`
- **CLI:** SimpleSerialShell, defaults to Telnet after setup; press any key on USB Serial to switch back

### Configuration

INI-style config file (`/config.ini`) on LittleFS, loaded at boot. Descriptor-table-driven: `cfg_table[]` in `config.cpp` maps `{section, key, type, offset}` to `conez_config_t` struct fields. Covers WiFi, GPS origin, LoRa radio params, timezone, debug defaults, and startup script. CLI commands: `config`, `config set`, `config unset`, `config reset`. See `documentation/config.txt` for full reference.

### Filesystem

LittleFS on 4MB flash partition. Stores BASIC scripts (`.bas`), LUT data files (`LUT_N.csv`), binary cue files (`.cue`), and optionally `/config.ini`. The configured startup script (default `/startup.bas`) auto-executes on boot if present.

### Firmware Versioning

`patch_firmware_ver.py` runs post-build to embed version/timestamp into the firmware binary. Configured via `custom_prog_*` fields in `platformio.ini`.

### Cue System

GPS-time-synced LED cueing engine for music-synchronized light shows across geographically distributed cones. Binary `.cue` files on LittleFS contain a sorted timeline of cue entries (fill, blackout, stop, effect) that the engine walks during playback. Each cue supports group targeting (per-cone, per-group, bitmask) and spatial delay modes (radial ripple, directional waves) computed from GPS position.

**Firmware files:** `cue.h`, `cue.cpp`. Initialized via `cue_setup()` in `setup()`, ticked via `cue_loop()` in the main loop.

**Binary format:** 64-byte header (magic `0x43554530`, version, num_cues, record_size) followed by 64-byte cue entries sorted by start_ms. See `cue_header` and `cue_entry` structs in `cue.h`.

**CLI commands:** `cue load <path>`, `cue start [ms]`, `cue stop`, `cue status`.

**Authoring tool:** `tools/cuetool.py` compiles human-editable YAML show descriptions into binary `.cue` files. Also supports dumping `.cue` files back to readable text. Requires PyYAML. See `tools/example_show.yaml` for the input format.

```bash
python3 tools/cuetool.py build tools/example_show.yaml -o firmware/data/show.cue
python3 tools/cuetool.py dump firmware/data/show.cue
```
