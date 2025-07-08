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


#define USE_TELNET


#define FSLINK LittleFS
#include "commands.h"

Stream *OutputStream = NULL;

// LED buffers
CRGB leds1[NUM_LEDS1];
CRGB leds2[NUM_LEDS2];
CRGB leds3[NUM_LEDS3];
CRGB leds4[NUM_LEDS4];


// Debug message config
uint32_t debug = DEBUG_MSG_LORA | DEBUG_MSG_GPS;

//I2C speed
#define I2C_FREQ      100000 // 400 kHz fast-mode; drop to 100 k if marginal


#ifndef BUILD_VERSION
#define BUILD_VERSION "unknown"
#endif
#ifndef BUILD_DATE
#define BUILD_DATE "unknown"
#endif
#ifndef BUILD_TIME
#define BUILD_TIME "unknown"
#endif


const char *wifi_ssid = "RN-ConeZ";
const char *wifi_psk = "conezconez";
static char hostname[17];


void init_LittleFS()
{
  Serial.println("---- LittleFS ----");


  if (!FSLINK.begin(true)) {
    Serial.println("Failed to mount LittleFS, even after formatting.");
    return;
  }

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
        Serial.printf( "  I2C error %d @ 0x%02X\n", err,addr );
    }
  }

  if ( !found )
    Serial.println( "  No I2C devices found" );
}


//LED Stuff
void blink_leds(CRGB col)
{
    leds1[0] = CRGB::Black;
    //Set LED's
    FastLED.show();
    delay(300);
    leds1[0] = col;
    //Set LED's
    FastLED.show();
    delay(500);
    leds1[0] = CRGB::Black;
    //Set LED's
    FastLED.show();
    delay(300);      
}

void color_leds(int ch, CRGB col)
{
    switch (ch)
    {
      default:
      case 1:
        for (int ii=0;ii<NUM_LEDS1;ii++)
        {
          leds1[ii] = col;    
        }
        break;
      case 2:
        for (int ii=0;ii<NUM_LEDS2;ii++)
        {
          leds2[ii] = col;    
        }
        break;
      case 3:
        for (int ii=0;ii<NUM_LEDS3;ii++)
        {
          leds3[ii] = col;    
        }
        break;
      case 4:
        for (int ii=0;ii<NUM_LEDS4;ii++)
        {
          leds4[ii] = col;    
        }
        break;
      }                  
    FastLED.show();
}


void check_serial(void)
{
  //We check for any incoming serial data
  //If there is data we switch all data from telnet 
  //to the USB serial port 

  if (Serial.available())
  {
    OutputStream = &Serial;
    setCLIEcho(true);
    init_commands(OutputStream);
  }
}

void setup()
{
  // Give some time to reconnect USB CDC serial console.
  delay( 5000 );

  // LED pin
  pinMode( LED_PIN, OUTPUT );
  digitalWrite( LED_PIN, LOW );
  delay( 500 );
  digitalWrite( LED_PIN, HIGH );
  delay( 500 );
  digitalWrite( LED_PIN, LOW );


  Serial.begin( 115200 );

  //WAIT FOR SERIAL USB PORT TO CONNECXT BEOFRE CONTINUING
  while (!Serial) {
    ; // do nothing
  }


  OutputStream = &Serial;
  OutputStream->println();
  OutputStream->println( "Starting...\n" );


  // Turn on LOAD FET
  pinMode( LOAD_ON_PIN, OUTPUT );
  digitalWrite( LOAD_ON_PIN, HIGH );

  // Turn on solar FET
  pinMode( SOLAR_PWM_PIN, OUTPUT );
  digitalWrite( SOLAR_PWM_PIN, HIGH );


  //Setup RGB leds so we can also signal stuff there...
  FastLED.addLeds<WS2811, RGB1_PIN, RGB>(leds1, NUM_LEDS1);
  FastLED.addLeds<WS2811, RGB2_PIN, RGB>(leds2, NUM_LEDS2);
  FastLED.addLeds<WS2811, RGB3_PIN, RGB>(leds3, NUM_LEDS3);
  FastLED.addLeds<WS2811, RGB4_PIN, RGB>(leds4, NUM_LEDS4);
  blink_leds(CRGB::Red);
  //NOTE: Don't use FastLED functions outside the setup routine in the main program.
  //At the end of Setup we create the badsic interpreter task who will handle all LED access 
  //through fastLED afterwards to avoid any thred collisions.


  dump_partitions();
  //dump_nvs();
  print_nvs_stats();
  init_LittleFS();
  list_dir( FSLINK, "/" );


  // I2C
  Wire.begin( I2C_SDA_PIN, I2C_SCL_PIN, I2C_FREQ );
  dump_i2c( Wire );


  // Fire up the LoRa radio.
  lora_setup();


  OutputStream->print( "\nConnecting to wifi..." );
  

  // Generate ConeZ-nnnn DHCP hostname from last 2 octets of MAC address.
  WiFi.mode( WIFI_STA );

  uint8_t mac[6];
  esp_read_mac( mac, ESP_MAC_WIFI_STA );

  //char hostname[16];                       // "ConeZ-" + 4 hex + NUL
  sprintf( hostname, "ConeZ-%02x%02x", mac[4], mac[5] );

  WiFi.setHostname( hostname );              // must precede WiFi.begin()

  OutputStream->print( "Hostname: " );
  OutputStream->println( hostname );

  WiFi.begin( wifi_ssid, wifi_psk );

  while( WiFi.status() != WL_CONNECTED )
  {
    delay( 500 );
    OutputStream->print( "." );
  }

  OutputStream->println( " Connected");
  OutputStream->print( "IP address: " );
  OutputStream->println( WiFi.localIP() );

  http_setup();

  //At this point switch comms over to telnet
  TelnetStream2.begin();
  OutputStream->println( "Telnet Initalized");
  OutputStream->println( "CLI active");

#ifdef USE_TELNET
  OutputStream->println( "CLI now via Telnet. Press any key to return to Serial");
  setCLIEcho(false);
  OutputStream = &TelnetStream2;
#endif
  //Init command Line interpreter
  init_commands(OutputStream);

  
  //Start Thread for Basic interpreter/FastLED here
  //setup_basic();
}


void loop()
{
  http_loop();

  //Run Shell commands
  run_commands();

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

  check_serial();

  //while( GPSSerial.available() )
  //  Serial.write( GPSSerial.read() );
}

