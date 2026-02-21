#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "main.h"
#include <WiFi.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "esp_heap_caps.h"
#include "esp_clk.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LittleFS.h>
#include <FS.h>
#include <RadioLib.h>
#include <esp_wifi.h>
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
#include "psram.h"
#include "conez_mqtt.h"

#define WAIT_FOR_USB_SERIAL
#define WAIT_FOR_USB_SERIAL_TIMEOUT 15    // Seconds

#define WIFI_TIMEOUT                5     // Seconds

#include "commands.h"


// Debug message config -- defined in printManager.cpp
//uint32_t debug = DEBUG_MSG_LORA | DEBUG_MSG_LORA_RAW |
//                 DEBUG_MSG_GPS | DEBUG_MSG_GPS_RAW;

//I2C speed
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
  Serial.println("---- LittleFS ----");


  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS, even after formatting.");
    return;
  }

  littlefs_mounted = true;
  Serial.println("LittleFS mounted successfully.");

  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes();

  Serial.println("LittleFS Stats:");
  Serial.printf("  Total bytes : %u\n", total);
  Serial.printf("  Used bytes  : %u\n", used);
  Serial.printf("  Free bytes  : %u\n", total - used);
  Serial.println( "" );
}


void list_dir(fs::FS &fs, const char *dirname, uint8_t levels = 1) 
{
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.printf("  [DIR ] %s\n", file.name());
      if (levels > 0) {
        list_dir(fs, file.path(), levels - 1);
      }
    } else {
      Serial.printf("  [FILE] %s\t(%u bytes)\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}


// Enumerate all I2C devices
void dump_i2c(void)
{
  int found = 0;

  Serial.print( "\nEnumerating I2C devices:\n" );
  for( uint8_t addr = 1; addr < 0x7F; ++addr )
  {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);

    if( err == ESP_OK )
    {
      Serial.printf( "  I2C device @ 0x%02X\n", addr );
      ++found;
    }
  }

  if ( !found )
    Serial.println( "  No I2C devices found" );
}


//LED Stuff -- led.cpp owns FastLED now; helpers here are setup-time only
#ifdef BOARD_HAS_RGB_LEDS
static void blink_leds(CRGB col)
{
    leds1[0] = CRGB::Black;
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(300));
    leds1[0] = col;
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(500));
    leds1[0] = CRGB::Black;
    led_show_now();
    vTaskDelay(pdMS_TO_TICKS(300));
}
#endif


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

  // LED pin
  gpio_set_direction( (gpio_num_t)LED_PIN, GPIO_MODE_OUTPUT );
  gpio_set_level( (gpio_num_t)LED_PIN, 0 );
  vTaskDelay(pdMS_TO_TICKS(500));
  gpio_set_level( (gpio_num_t)LED_PIN, 1 );
  vTaskDelay(pdMS_TO_TICKS(500));
  gpio_set_level( (gpio_num_t)LED_PIN, 0 );


  Serial.begin( 115200 );

  //WAIT FOR SERIAL USB PORT TO CONNECXT BEOFRE CONTINUING
  #ifdef WAIT_FOR_USB_SERIAL
    unsigned long t_start = uptime_ms();

    while (!Serial)
    {
      #ifdef WAIT_FOR_USB_SERIAL_TIMEOUT
        if( uptime_ms() - t_start > WAIT_FOR_USB_SERIAL_TIMEOUT * 1000 )
          break;
      #endif
    }
  #endif


  Serial.println();

  // Reset ANSI state and clear any leftover bootloader color, then print banner
  Serial.print("\033[0m\r");
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t desc;
    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK)
      Serial.printf("%s firmware v%s (%s %s)\n", desc.project_name, desc.version, desc.date, desc.time);
    else
      Serial.println("ConeZ");

#ifdef BOARD_CONEZ_V0_1
    Serial.println("Board:  conez-v0-1");
#elif defined(BOARD_HELTEC_LORA32_V3)
    Serial.println("Board:  heltec-lora32-v3");
#else
    Serial.println("Board:  unknown");
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
      Serial.printf("CPU:    %s rev %d, %u MHz, %d cores\n",
          model, ci.revision,
          (unsigned)(esp_clk_cpu_freq() / 1000000), ci.cores);
    }
    Serial.printf("Flash:  %u KB, SRAM: %u KB free / %u KB total\n",
        (unsigned)spi_flash_get_chip_size() / 1024,
        (unsigned)esp_get_free_heap_size() / 1024,
        (unsigned)heap_caps_get_total_size(MALLOC_CAP_8BIT) / 1024);
#ifdef BOARD_HAS_IMPROVISED_PSRAM
    Serial.println("PSRAM:  8 MB (external SPI)");
#elif defined(BOARD_HAS_NATIVE_PSRAM)
    Serial.printf("PSRAM:  %u KB (native)\n", (unsigned)(esp_spiram_get_size() / 1024));
#else
    Serial.println("PSRAM:  none");
#endif
    Serial.println();
  }

#ifdef BOARD_HAS_BUZZER
  for (int ii = 1100; ii < 13100; ii += 1000)
  {
    Serial.printf("\rSpeaker: %d Hz  ", ii);
    buzzer(ii, 128);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  buzzer(20000, 0);
  Serial.println("\rSpeaker: OK          \n");
#endif

  dump_partitions();
  //dump_nvs();
  print_nvs_stats();
  init_LittleFS();
  //list_dir( LittleFS, "/" );

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

  //NOTE: After led_start_task(), only the LED render task calls FastLED.show().
  //All other code writes to the LED buffers and calls led_show() to mark dirty.
#endif

  // I2C
  {
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = I2C_SDA_PIN;
    i2c_conf.scl_io_num = I2C_SCL_PIN;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_FREQ;
    i2c_param_config(I2C_NUM_0, &i2c_conf);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
  }
  dump_i2c();


  // Fire up the LoRa radio.
  lora_setup();

#ifdef BOARD_HAS_GPS
  // Fire up GPS UART.
  gps_setup();
#endif

  //Setup Sensors
  sensors_setup();


  if (config.wifi_enabled) {
    Serial.println( "\nConnecting to wifi..." );

    // Generate DHCP hostname: use config.device_name if set, else ConeZ-nnnn from MAC.
    WiFi.mode( WIFI_STA );

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

    WiFi.setHostname( hostname );              // must precede WiFi.begin()

    Serial.print( "Hostname: " );
    Serial.println( hostname );

    WiFi.begin( config.wifi_ssid, config.wifi_password );

    unsigned long t_wifi_start = uptime_ms();

    while( WiFi.status() != WL_CONNECTED && uptime_ms() - t_wifi_start < WIFI_TIMEOUT * 1000 )
    {
      vTaskDelay(pdMS_TO_TICKS(500));
      Serial.print( "." );
    }

    if( WiFi.status() == WL_CONNECTED )
    {
      Serial.println( " Connected");
      Serial.print( "IP address: " );
      Serial.println( WiFi.localIP() );

      // Start NTP time sync (provides time on all boards, fills in before GPS lock)
      ntp_setup();
    }
    else
    {
      Serial.println("");
      Serial.println("WiFi timed out");
    }
  } else {
    Serial.println( "\nWiFi disabled" );
    WiFi.mode( WIFI_OFF );
  }

  http_setup();

  //Start telnet server and dual-stream CLI
  telnet.begin();
  Serial.println( "Telnet server started");

  //Init print manager (all output goes to both Serial + Telnet)
  printManagerInit(&dualStream);
  config_apply_debug();
  showTimestamps(true);

  // MQTT uses printfnl() — must come after printManagerInit()
  mqtt_setup();

  sunSetTZOffset(config.timezone);

  //Start the LED render task (owns FastLED.show() from here on)
  led_start_task();

  //Start scripting runtime tasks
#ifdef INCLUDE_BASIC
  setup_basic();
  Serial.println("BASIC task active");
#endif
#ifdef INCLUDE_WASM
  setup_wasm();
  Serial.println("WASM task active");
#endif

  // ANSI color test — each letter in a different color
  Serial.print("\nANSI color test: ");
  const char *hello = "Hello World";
  const int colors[] = { 31, 32, 33, 34, 35, 36, 91, 92, 93, 94, 95 };
  for (int i = 0; hello[i]; i++) {
    Serial.printf("\033[%dm%c", colors[i], hello[i]);
  }
  Serial.println("\033[0m");

  //Init command line interpreter (single DualStream for both Serial + Telnet)
  setCLIEcho(true);
  init_commands(&dualStream);
  // Suppress ESP-IDF component logging — with ARDUINO_USB_CDC_ON_BOOT=1
  // it shares the USB CDC with Serial, bypassing our print_mutex.
  esp_log_level_set("*", ESP_LOG_NONE);

  Serial.println("CLI active on Serial + Telnet\n");
  shell.showPrompt();

  xTaskCreatePinnedToCore(shell_task_fun, "ShellTask", 8192, NULL, 1, NULL, 1);
}


void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1));

  inc_thread_count(xPortGetCoreID());

  //HTTP Request Processor
  http_loop();

  // put your main code here, to run repeatedly:
  //Serial.print( "." );
  //Serial.print( lora_rxdone_flag );
  //delay( 1000 );
  //digitalWrite( LED_PIN, HIGH );
  //delay( 1000 );
  //digitalWrite( LED_PIN, LOW );

  if( uptime_ms() % 500 > 250 )
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

#ifdef BOARD_HAS_RGB_LEDS
  //RUN Direct Effects
  //CIRCLE_effect();
  //SOS_effect2();
#endif

}

// --- ESP-IDF entry point (Arduino as IDF component) ---
// Replaces CONFIG_AUTOSTART_ARDUINO — gives us full control over
// loopTask stack size and core affinity.
static void loopTask(void *pvParameters)
{
    setup();
    for (;;) {
        loop();
    }
}

extern "C" void app_main()
{
    initArduino();
    xTaskCreatePinnedToCore(loopTask, "loopTask", 8192, NULL, 1, NULL, 1);
}
