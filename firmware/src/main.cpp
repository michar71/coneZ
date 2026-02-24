#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "main.h"
#include "conez_wifi.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "spi_flash_mmap.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "esp_littlefs.h"
#include <RadioLib.h>
#include "conez_usb.h"
#include "dualstream.h"
#include "shell.h"
#include "basic_wrapper.h"
#ifdef INCLUDE_WASM
#include "wasm_wrapper.h"
#endif
#include "lora.h"
#include "fwupdate.h"
#include "http.h"
#include "gps.h"
#include "effects.h"
#include "printManager.h"
#include "sensors.h"
#include "sun.h"
#include "config.h"
#include "cue.h"
#include "lut.h"
#include "adc.h"
#include "psram.h"
#include "conez_mqtt.h"
#include "loadavg.h"

#define WIFI_TIMEOUT                5     // Seconds

#include "commands.h"


// Debug message config -- defined in printManager.cpp
//uint32_t debug = DEBUG_MSG_LORA | DEBUG_MSG_LORA_RAW |
//                 DEBUG_MSG_GPS | DEBUG_MSG_GPS_RAW;

// I2C bus handle — shared with sensors.cpp, mpu6500.cpp
i2c_master_bus_handle_t i2c_bus = NULL;

// I2C speed
#define I2C_FREQ      100000 // 100 kHz standard-mode


#ifndef BUILD_VERSION
#define BUILD_VERSION "unknown"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "unknown"
#endif


bool littlefs_mounted = false;

void init_LittleFS()
{
  usb_printf("---- LittleFS ----\n");

  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = LFS_PREFIX;
  conf.partition_label = "spiffs";   // legacy name in partitions.csv
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;

  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    usb_printf("Failed to mount LittleFS: %s\n", esp_err_to_name(err));
    return;
  }

  littlefs_mounted = true;
  usb_printf("LittleFS mounted successfully.\n");

  size_t total = 0, used = 0;
  esp_littlefs_info("spiffs", &total, &used);

  usb_printf("LittleFS Stats:\n");
  usb_printf("  Total bytes : %u\n", (unsigned)total);
  usb_printf("  Used bytes  : %u\n", (unsigned)used);
  usb_printf("  Free bytes  : %u\n", (unsigned)(total - used));
  usb_printf("\n");
}


// Enumerate all I2C devices using probe
void dump_i2c(void)
{
  int found = 0;

  usb_printf("\nEnumerating I2C devices:\n");
  for( uint16_t addr = 1; addr < 0x7F; ++addr )
  {
    if (i2c_master_probe(i2c_bus, addr, pdMS_TO_TICKS(50)) == ESP_OK)
    {
      usb_printf("  I2C device @ 0x%02X\n", addr);
      ++found;
    }
  }

  if ( !found )
    usb_printf("  No I2C devices found\n");
}


//LED Stuff -- led.cpp owns hardware output; helpers here are setup-time only


DualStream dualStream;


void script_autoexec(void);

static void shell_task_fun(void *param)
{
    script_autoexec();
    for (;;) {
        run_commands();
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}


extern int cmd_compile(int argc, char **argv);

void script_autoexec(void)
{
  static bool startup = true;
  if (!startup) return;
  startup = false;

  if (!littlefs_mounted) return;

  // If user configured a specific startup script, use it
  if (config.startup_script[0] != '\0') {
      if (file_exists(config.startup_script)) {
          printfnl(SOURCE_SYSTEM, "%s found. Executing...\n", config.startup_script);
          set_script_program(config.startup_script);
      } else {
          printfnl(SOURCE_SYSTEM, "No %s\n", config.startup_script);
      }
      return;
  }

  // Auto-detect: try candidates in priority order based on compiled features
  static const struct { const char *path; int need; } candidates[] = {
#ifdef INCLUDE_BASIC
      { "/startup.bas",  0 },
#endif
#ifdef INCLUDE_C_COMPILER
      { "/startup.c",    1 },  // needs compile-then-run
#endif
#ifdef INCLUDE_WASM
      { "/startup.wasm", 0 },
#endif
  };

  for (int i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
      if (!file_exists(candidates[i].path)) continue;
      printfnl(SOURCE_SYSTEM, "%s found. Executing...\n", candidates[i].path);
      if (candidates[i].need) {
          char *argv[] = { (char *)"compile", (char *)candidates[i].path, (char *)"run" };
          cmd_compile(3, argv);
      } else {
          set_script_program((char *)candidates[i].path);
      }
      return;
  }

  printfnl(SOURCE_SYSTEM, "No startup script found\n");
}


#ifdef BOARD_HAS_BUZZER
void buzzer(int freq, int vol)
{
  ledc_timer_config_t timer_conf = {};
  timer_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_conf.duty_resolution = LEDC_TIMER_8_BIT;
  timer_conf.timer_num = LEDC_TIMER_0;
  timer_conf.freq_hz = (uint32_t)freq;
  timer_conf.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_conf);

  ledc_channel_config_t ch_conf = {};
  ch_conf.gpio_num = BUZZER_PIN;
  ch_conf.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_conf.channel = LEDC_CHANNEL_0;
  ch_conf.timer_sel = LEDC_TIMER_0;
  ch_conf.duty = (uint32_t)vol;
  ch_conf.hpoint = 0;
  ledc_channel_config(&ch_conf);

  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, (uint32_t)vol);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
#endif


void setup()
{

#ifdef BOARD_HAS_POWER_MGMT
  // Turn on LOAD FET
  gpio_set_direction( (gpio_num_t)LOAD_ON_PIN, GPIO_MODE_OUTPUT );
  gpio_set_level( (gpio_num_t)LOAD_ON_PIN, 1 );

  // Turn on solar FET
  gpio_set_direction( (gpio_num_t)SOLAR_PWM_PIN, GPIO_MODE_OUTPUT );
  gpio_set_level( (gpio_num_t)SOLAR_PWM_PIN, 1 );
#endif

  vTaskDelay(pdMS_TO_TICKS(250));

  vTaskDelay(pdMS_TO_TICKS(1000));

#ifdef BOARD_HAS_BUZZER
  //Buzzer Setup
  gpio_set_direction( (gpio_num_t)BUZZER_PIN, GPIO_MODE_OUTPUT );
#endif

  // LED pin — gpio_reset_pin() sets IO MUX to GPIO function.
  // Required for GPIO 40 (ConeZ) which defaults to JTAG MTDO on ESP32-S3.
  gpio_reset_pin( (gpio_num_t)LED_PIN );
  gpio_set_direction( (gpio_num_t)LED_PIN, GPIO_MODE_OUTPUT );
  gpio_set_level( (gpio_num_t)LED_PIN, 0 );
  vTaskDelay(pdMS_TO_TICKS(500));
  gpio_set_level( (gpio_num_t)LED_PIN, 1 );
  vTaskDelay(pdMS_TO_TICKS(500));
  gpio_set_level( (gpio_num_t)LED_PIN, 0 );


  usb_init();

  // Wait for USB host to connect (up to 5 s).
  // After hard reset the device re-enumerates on the bus; the host terminal
  // needs time to re-open the port.  Without this wait, boot output fills
  // the TX ring buffer, times out (10 ms), and is silently dropped.
  {
    uint32_t t0 = uptime_ms();
    while (uptime_ms() - t0 < 5000) {
      if (usb_connected()) break;
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(200));   // extra settle for terminal app
  }

  usb_printf("\n");

  // Reset ANSI state and clear any leftover bootloader color, then print banner
  usb_printf("\033[0m\r");
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK)
      usb_printf("%s firmware v%s (%s %s)\n", desc.project_name, desc.version, desc.date, desc.time);
    else
      usb_printf("ConeZ\n");

#ifdef BOARD_CONEZ_V0_1
    usb_printf("Board:  conez-v0-1\n");
#elif defined(BOARD_HELTEC_LORA32_V3)
    usb_printf("Board:  heltec-lora32-v3\n");
#else
    usb_printf("Board:  unknown\n");
#endif

    {
      esp_chip_info_t ci;
      esp_chip_info(&ci);
      const char *model = "ESP32";
      switch (ci.model) {
        case CHIP_ESP32:   model = "ESP32";    break;
        case CHIP_ESP32S2: model = "ESP32-S2"; break;
        case CHIP_ESP32S3: model = "ESP32-S3"; break;
        case CHIP_ESP32C3: model = "ESP32-C3"; break;
        default: break;
      }
      usb_printf("CPU:    %s rev %d, %u MHz, %d cores\n",
          model, ci.revision,
          (unsigned)(esp_clk_cpu_freq() / 1000000), ci.cores);
    }
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    usb_printf("Flash:  %u KB, SRAM: %u KB free / %u KB total\n",
        (unsigned)flash_size / 1024,
        (unsigned)esp_get_free_heap_size() / 1024,
        (unsigned)heap_caps_get_total_size(MALLOC_CAP_8BIT) / 1024);
#ifdef BOARD_HAS_IMPROVISED_PSRAM
    usb_printf("PSRAM:  8 MB (external SPI)\n");
#elif defined(BOARD_HAS_NATIVE_PSRAM)
    usb_printf("PSRAM:  %u KB (native)\n", (unsigned)(esp_spiram_get_size() / 1024));
#else
    usb_printf("PSRAM:  none\n");
#endif
    usb_printf("\n");
  }

#ifdef BOARD_HAS_BUZZER
  for (int ii = 1100; ii < 13100; ii += 1000)
  {
    usb_printf("\rSpeaker: %d Hz  ", ii);
    buzzer(ii, 128);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  buzzer(20000, 0);
  usb_printf("\rSpeaker: OK          \n\n");
#endif

  dump_partitions();

  // NVS — initArduino() used to handle this; now we do it explicitly.
  {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    if (err != ESP_OK) {
      usb_printf("NVS init failed: %s\n", esp_err_to_name(err));
    }
  }

  //dump_nvs();
  print_nvs_stats();
  init_LittleFS();

  // Load config from /config.ini (or use compiled defaults)
  config_init();

  // Seed time from compile timestamp (fallback until GPS or NTP locks)
  time_seed_compile();

  // Initialize LUT mutex (before any scripting tasks start)
  lutMutexInit();

  // Initialize cue engine (no file loaded yet)
  cue_setup();

  // Initialize external PSRAM (ConeZ PCB only)
  psram_setup();

  // Initialize debug log ring buffer (uses PSRAM when available)
  log_init();

#ifdef BOARD_HAS_RGB_LEDS
  //Setup RGB leds (buffers sized from config)
  led_setup();

    led_set_channel(1, 4, CRGB::Red);
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(500));
    led_set_channel(1, 4, CRGB::Green);
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(500));
    led_set_channel(1, 4, CRGB::Blue);
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(500));
    led_set_channel(1, 4, CRGB::Black);
    led_show_now();

  // Apply default colors from config (if any channel has a non-black color)
  {
    int colors[] = { config.led_color1, config.led_color2, config.led_color3, config.led_color4 };
    int counts[] = { config.led_count1, config.led_count2, config.led_count3, config.led_count4 };
    for (int ch = 0; ch < 4; ch++) {
      if (colors[ch] != 0) {
        CRGB c((colors[ch] >> 16) & 0xFF, (colors[ch] >> 8) & 0xFF, colors[ch] & 0xFF);
        led_set_channel(ch + 1, counts[ch], c);
      }
    }
    led_show_now();
  }

  //NOTE: After led_start_task(), only the LED render task pushes to hardware.
  //All other code writes to the LED buffers and calls led_show() to mark dirty.
#endif

  // I2C — new handle-based driver (IDF 5.x)
  {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = I2C_NUM_0;
    bus_cfg.sda_io_num = (gpio_num_t)I2C_SDA_PIN;
    bus_cfg.scl_io_num = (gpio_num_t)I2C_SCL_PIN;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    i2c_new_master_bus(&bus_cfg, &i2c_bus);
  }
  dump_i2c();


  // Fire up the LoRa radio.
  lora_setup();

#ifdef BOARD_HAS_GPS
  // Fire up GPS UART.
  gps_setup();
#endif

  // Initialize ADC (for battery/solar voltage)
  adc_setup();

  //Setup Sensors
  sensors_setup();


  // Initialize WiFi subsystem (netif, event loop, event handlers)
  wifi_init();

  if (config.wifi_enabled) {
    usb_printf("\nConnecting to wifi...\n");

    // Generate DHCP hostname: use config.device_name if set, else ConeZ-nnnn from MAC.
    char hostname[CONFIG_MAX_DEVICE_NAME];
    if (config.device_name[0] != '\0')
    {
      strlcpy( hostname, config.device_name, sizeof(hostname) );
    }
    else
    {
      uint8_t mac[6];
      esp_read_mac( mac, ESP_MAC_WIFI_STA );
      sprintf( hostname, "ConeZ-%02x%02x", mac[4], mac[5] );
    }

    usb_printf("Hostname: %s\n", hostname);

    wifi_start( config.wifi_ssid, config.wifi_password, hostname );

    unsigned long t_wifi_start = uptime_ms();

    while( !wifi_is_connected() && uptime_ms() - t_wifi_start < WIFI_TIMEOUT * 1000 )
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      usb_printf(".");
    }

    if( wifi_is_connected() )
    {
      char ip[16];
      wifi_get_ip_str(ip, sizeof(ip));
      usb_printf(" Connected\nIP address: %s\n", ip);

      // Start NTP time sync (provides time on all boards, fills in before GPS lock)
      ntp_setup();
    }
    else
    {
      usb_printf("\nWiFi timed out\n");
    }
  } else {
    usb_printf("\nWiFi disabled\n");
  }

  http_setup();

  //Start telnet server and dual-stream CLI
  telnet.begin();
  usb_printf("Telnet server started\n");

  //Init print manager (all output goes to both USB + Telnet)
  printManagerInit(&dualStream);
  config_apply_debug();
  showTimestamps(true);

  // MQTT uses printfnl() — must come after printManagerInit()
  mqtt_setup();

  sunSetTZOffset(config.timezone);

  //Start the LED render task (owns RMT output from here on)
  led_start_task();

  //Start scripting runtime tasks
#ifdef INCLUDE_BASIC
  setup_basic();
  usb_printf("BASIC task active\n");
#endif
#ifdef INCLUDE_WASM
  setup_wasm();
  usb_printf("WASM task active\n");
#endif

  // ANSI color test — each letter in a different color
  usb_printf("\nANSI color test: ");
  const char *hello = "Hello World";
  const int colors[] = { 31, 32, 33, 34, 35, 36, 91, 92, 93, 94, 95 };
  for (int i = 0; hello[i]; i++) {
    usb_printf("\033[%dm%c", colors[i], hello[i]);
  }
  usb_printf("\033[0m\n");

  //Init command line interpreter (single DualStream for both USB + Telnet)
  setCLIEcho(true);
  init_commands(&dualStream);
  // Suppress ESP-IDF component logging — shares USB CDC, bypasses print_mutex.
  esp_log_level_set("*", ESP_LOG_NONE);

  usb_printf("CLI active on USB + Telnet\n\n");
  shell.showPrompt();

  xTaskCreatePinnedToCore(shell_task_fun, "ShellTask", 8192, NULL, 1, NULL, tskNO_AFFINITY);
}


void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1));

  inc_thread_count(xPortGetCoreID());

  //HTTP Request Processor
  http_loop();

  if( uptime_ms() % 1000 < 250 )
    gpio_set_level( (gpio_num_t)LED_PIN, 1 );
  else
    gpio_set_level( (gpio_num_t)LED_PIN, 0 );

  // Check for LoRa packets
  lora_rx();

#ifdef BOARD_HAS_GPS
  // Process GPS messages
  gps_loop();

  {
    static int64_t last_sun_update = 0;
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_sun_update >= 60000000LL) {
        last_sun_update = now_us;
        sunUpdateViaGPS();
    }
  }
#endif

  // NTP time sync (runs on all boards when WiFi is connected)
  ntp_loop();

  // MQTT client
  mqtt_loop();

  //Proicess Sensors.. Maybe we shoudl run this slower?
  sensors_loop();

  // Cue timeline engine
  cue_loop();

  // CPU load average sampling (5-second EWMA)
  loadavg_sample();

#ifdef BOARD_HAS_RGB_LEDS
  //RUN Direct Effects
  //CIRCLE_effect();
  //SOS_effect2();
#endif

}

// --- ESP-IDF entry point ---
static void loopTask(void *pvParameters)
{
    setup();
    for (;;) {
        loop();
    }
}

extern "C" void app_main()
{
    xTaskCreatePinnedToCore(loopTask, "loopTask", 4096, NULL, 1, NULL, 1);
}
