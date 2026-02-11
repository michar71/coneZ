#include <Arduino.h>
#include <Wire.h>
#include "main.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <LittleFS.h>
#include <FS.h>
#include <RadioLib.h>
#include <esp_wifi.h>
#include <TelnetStream2.h>
#include "basic_wrapper.h"
#include "lora.h"
#include "fwupdate.h"
#include "http.h"
#include "gps.h"
#include "effects.h"
#include "printManager.h"
#include "sensors.h"
#include "sun.h"
#include "config.h"

#define USE_TELNET

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


  if (!FSLINK.begin(true)) {
    Serial.println("Failed to mount LittleFS, even after formatting.");
    return;
  }

  littlefs_mounted = true;
  Serial.println("LittleFS mounted successfully.");

  size_t total = FSLINK.totalBytes();
  size_t used = FSLINK.usedBytes();

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


void check_serial(void)
{
  //We check for any incoming serial data
  //If there is data we switch all data from telnet
  //to the USB serial port
  static bool serial_active = false;

  if (Serial.available())
  {
    if (!serial_active)
    {
      serial_active = true;
      setStream(&Serial);
      setCLIEcho(true);
      init_commands(getStream());
    }
  }
}


void basic_autoexec(void)
{
    //Hmm... Maybe this should actually happen in setup?
  static bool startup = true;  
  if (startup)
  {
    startup = false;

    //Appartly the function exist() exist... But shouldn't be used.
    //Because it uses open() which flags an error if the file doesn't exists...
    //Open bug in littleFS for 4 year... WTF????
    if (littlefs_mounted && FSLINK.exists(config.startup_script))
    {
        printfnl(SOURCE_SYSTEM,"%s found. Executing...\n", config.startup_script);
        set_basic_program(config.startup_script);
    }
    else
    {
      printfnl(SOURCE_SYSTEM,"No %s\n", config.startup_script);
    }
  }
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

#ifdef BOARD_HAS_RGB_LEDS
  //Setup RGB leds so we can also signal stuff there...
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

  
#ifdef BOARD_HAS_BUZZER
  for (int ii=1100;ii<13100;ii=ii+1000)
  {
    Serial.print("Freq:");
    Serial.println(ii);
    buzzer(ii,128);
    delay(100);
  }
  buzzer(20000,0);
#endif
   


  Serial.println();
  Serial.println( "Starting...\n" );


  dump_partitions();
  //dump_nvs();
  print_nvs_stats();
  init_LittleFS();
  list_dir( FSLINK, "/" );

  // Load config from /config.ini (or use compiled defaults)
  config_init();


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
  }
  else
  {
    Serial.println("");
    Serial.println("WiFi timed out");
  }

  http_setup();

  //At this point switch comms over to telnet
  TelnetStream2.begin();
  Serial.println( "Telnet initialized");
  Serial.println( "CLI active");

  //Init print manager
  printManagerInit(&Serial);
  config_apply_debug();
  showTimestamps(true);

  sunSetTZOffset(config.timezone);

  //Start the LED render task (owns FastLED.show() from here on)
  led_start_task();

  //Start Thread for Basic interpreter
  setup_basic();
  Serial.println("BASIC task active\n");

#ifdef USE_TELNET
  Serial.println("CLI now via Telnet. Press any key to return to Serial\n");
  setCLIEcho(false);
  setStream(&TelnetStream2);
#endif
  //Init command Line interpreter
  init_commands(getStream());
}


void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1));

  inc_thread_count(xPortGetCoreID());

  //HTTP Request Processor
  http_loop();

  //Run Shell commands and check serial port. Protected bymutex.
  run_commands();
  check_serial();

  //Check for startup.bas and if it exist run it once
  basic_autoexec();

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

  //Proicess Sensors.. Maybe we shoudl run this slower?
  sensors_loop();

#ifdef BOARD_HAS_RGB_LEDS
  //RUN Direct Effects
  //CIRCLE_effect();
  SOS_effect2();
#endif

}
