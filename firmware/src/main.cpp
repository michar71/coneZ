#include <Arduino.h>
#include <Wire.h>
#include "main.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include "esp_system.h"
#include "esp_log.h"
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
#include "mqtt_client.h"

#define WAIT_FOR_USB_SERIAL
#define WAIT_FOR_USB_SERIAL_TIMEOUT 15    // Seconds

#define WIFI_TIMEOUT                10    // Seconds

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
void dump_i2c( TwoWire &bus )
{
  int found = 0;

  Serial.print( "\nEnumerating I2C devices:\n" );
  bus.setTimeOut(50);
  for( uint8_t addr = 1; addr < 0x7F; ++addr )
  {
    bus.beginTransmission( addr );
    uint8_t err = bus.endTransmission();

    if( err == 0 )
    {
      Serial.printf( "  I2C device @ 0x%02X\n", addr );
      ++found;
    }
    else
    {
        //Serial.printf( "  I2C error %d @ 0x%02X\n", err,addr );
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
    delay(300);
    leds1[0] = col;
    led_show_now();
    delay(500);
    leds1[0] = CRGB::Black;
    led_show_now();
    delay(300);
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
  //ledcSetup(0, freq, 8);
  //ledcAttachPin(BUZZER_PIN, 0);
  //ledcWrite(0, vol);
  analogWriteResolution(8);
  analogWriteFrequency(freq);
  analogWrite(BUZZER_PIN,vol);
}
#endif


void setup()
{

#ifdef BOARD_HAS_POWER_MGMT
  // Turn on LOAD FET
  pinMode( LOAD_ON_PIN, OUTPUT );
  digitalWrite( LOAD_ON_PIN, HIGH );

  // Turn on solar FET
  pinMode( SOLAR_PWM_PIN, OUTPUT );
  digitalWrite( SOLAR_PWM_PIN, HIGH );
#endif

  delay( 250 );

  delay(1000);

#ifdef BOARD_HAS_BUZZER
  //Buzzer Setup
  pinMode( BUZZER_PIN, OUTPUT );
#endif

  // LED pin
  pinMode( LED_PIN, OUTPUT );
  digitalWrite( LED_PIN, LOW );
  delay( 500 );
  digitalWrite( LED_PIN, HIGH );
  delay( 500 );
  digitalWrite( LED_PIN, LOW );


  Serial.begin( 115200 );

  //WAIT FOR SERIAL USB PORT TO CONNECXT BEOFRE CONTINUING
  #ifdef WAIT_FOR_USB_SERIAL
    unsigned long t_start = millis();

    while (!Serial)
    {
      #ifdef WAIT_FOR_USB_SERIAL_TIMEOUT
        if( millis() - t_start > WAIT_FOR_USB_SERIAL_TIMEOUT * 1000 )
          break;
      #endif
    }
  #endif


  Serial.println();

  // Print firmware version and hardware banner
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

    Serial.printf("CPU:    %s rev %d, %d MHz, %d cores\n",
        ESP.getChipModel(), ESP.getChipRevision(),
        ESP.getCpuFreqMHz(), ESP.getChipCores());
    Serial.printf("Flash:  %lu KB, SRAM: %lu KB free / %lu KB total\n",
        ESP.getFlashChipSize() / 1024,
        ESP.getFreeHeap() / 1024,
        ESP.getHeapSize() / 1024);
#ifdef BOARD_HAS_IMPROVISED_PSRAM
    Serial.println("PSRAM:  8 MB (external SPI)");
#elif defined(BOARD_HAS_NATIVE_PSRAM)
    Serial.printf("PSRAM:  %lu KB (native)\n", ESP.getPsramSize() / 1024);
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
    delay(100);
  }
  buzzer(20000, 0);
  Serial.println("\rSpeaker: OK          \n");
#endif

  dump_partitions();
  //dump_nvs();
  print_nvs_stats();
  init_LittleFS();
  list_dir( LittleFS, "/" );

  // Load config from /config.ini (or use compiled defaults)
  config_init();

  // Initialize LUT mutex (before any scripting tasks start)
  lutMutexInit();

  // Initialize cue engine (no file loaded yet)
  cue_setup();

  // Initialize external PSRAM (ConeZ PCB only)
  psram_setup();

#ifdef BOARD_HAS_RGB_LEDS
  //Setup RGB leds (buffers sized from config)
  led_setup();

    led_set_channel(1, 4, CRGB::Red);
    led_show_now();
    delay(500);
    led_set_channel(1, 4, CRGB::Green);
    led_show_now();
    delay(500);
    led_set_channel(1, 4, CRGB::Blue);
    led_show_now();
    delay(500);
    led_set_channel(1, 4, CRGB::Black);
    led_show_now();

  //NOTE: After led_start_task(), only the LED render task calls FastLED.show().
  //All other code writes to the LED buffers and calls led_show() to mark dirty.
#endif

  // I2C
  Wire.begin( I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ );
  dump_i2c( Wire );


  // Fire up the LoRa radio.
  lora_setup();

#ifdef BOARD_HAS_GPS
  // Fire up GPS UART.
  gps_setup();
#endif

  //Setup Sensors
  sensors_setup();


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

  unsigned long t_wifi_start = millis();

  while( WiFi.status() != WL_CONNECTED && millis() - t_wifi_start < WIFI_TIMEOUT * 1000 )
  {
    delay( 500 );
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

  mqtt_setup();

  http_setup();

  //Start telnet server and dual-stream CLI
  telnet.begin();
  Serial.println( "Telnet server started");

  //Init print manager (all output goes to both Serial + Telnet)
  printManagerInit(&dualStream);
  config_apply_debug();
  showTimestamps(true);

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

  if( millis() % 500 > 250 )
    digitalWrite( LED_PIN, HIGH );
  else
    digitalWrite( LED_PIN, LOW );

  // Check for LoRa packets
  lora_rx();

#ifdef BOARD_HAS_GPS
  // Process GPS messages
  gps_loop();

  EVERY_N_SECONDS( 60 )
  {
    // Update the sun position every minute
    sunUpdateViaGPS();
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
