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
#include "basic_wrapper.h"
#include "effects.h"


#define USE_TELNET

#define WAIT_FOR_USB_SERIAL
#define WAIT_FOR_USB_SERIAL_TIMEOUT 15    // Seconds

#define WIFI_TIMEOUT                30    // Seconds

#define FSLINK LittleFS
#include "commands.h"

Stream *OutputStream = NULL;

// LED buffers
CRGB leds1[NUM_LEDS1];
CRGB leds2[NUM_LEDS2];
CRGB leds3[NUM_LEDS3];
CRGB leds4[NUM_LEDS4];


// Debug message config
//uint32_t debug = DEBUG_MSG_LORA | DEBUG_MSG_LORA_RAW | 
//                 DEBUG_MSG_GPS | DEBUG_MSG_GPS_RAW;

uint32_t debug = 0;

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

void color_leds(int ch, int cnt, CRGB col)
{
    if (cnt > NUM_LEDS1)
      cnt = NUM_LEDS1;
    switch (ch)
    {
      default:
      case 1:
        for (int ii=0;ii<cnt;ii++)
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

void basic_autoexec(void)
{
    //Hmm... Maybe this should actually happen in setup?
  static bool startup = true;  
  if (startup)
  {
    startup = false;
    if (FSLINK.exists((char*)"/startup.bas"))
    {
        OutputStream->print("startup.bas found. Executing...");
        set_basic_program(OutputStream,"/startup.bas");
    }
    else
    {
      OutputStream->print("No startup.bas");
    }
  }
}

void buzzer(int freq, int vol)
{
  //ledcSetup(0, freq, 8);
  //ledcAttachPin(BUZZER_PIN, 0);
  //ledcWrite(0, vol);
  analogWriteResolution(8);
  analogWriteFrequency(freq);
  analogWrite(BUZZER_PIN,vol);
}

void setup()
{

  // Turn on LOAD FET
  pinMode( LOAD_ON_PIN, OUTPUT );
  digitalWrite( LOAD_ON_PIN, HIGH );

  // Turn on solar FET
  pinMode( SOLAR_PWM_PIN, OUTPUT );
  digitalWrite( SOLAR_PWM_PIN, HIGH );

  delay( 250 );


  //Setup RGB leds so we can also signal stuff there...
  FastLED.addLeds<WS2811, RGB1_PIN, BRG>(leds1, NUM_LEDS1);
  FastLED.addLeds<WS2811, RGB2_PIN, BRG>(leds2, NUM_LEDS2);
  FastLED.addLeds<WS2811, RGB3_PIN, BRG>(leds3, NUM_LEDS3);
  FastLED.addLeds<WS2811, RGB4_PIN, BRG>(leds4, NUM_LEDS4);


    color_leds(1,4,CRGB::Red);
    delay(500);
    color_leds(1,4,CRGB::Green);
    delay(500);
    color_leds(1,4,CRGB::Blue);
    delay(500);
    color_leds(1,4,CRGB::Black);
  
  //NOTE: Don't use FastLED functions outside the setup routine in the main program.
  //At the end of Setup we create the BASIC interpreter task who will handle all LED access 
  //through fastLED afterwards to avoid any thread collisions.


  delay(1000);

  //Buzzer Setup
  pinMode( BUZZER_PIN, OUTPUT );

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

  /*
  for (int ii=1100;ii<30000;ii=ii+1000)
  {
    Serial.print("Freq:");
    Serial.println(ii);
    buzzer(ii,255);
    delay(500);
  }
  */ 


  OutputStream = &Serial;
  OutputStream->println();
  OutputStream->println( "Starting...\n" );


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

  // Fire up GPS UART.
  gps_setup();


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

  unsigned long t_wifi_start = millis();

  while( WiFi.status() != WL_CONNECTED && millis() - t_wifi_start < WIFI_TIMEOUT * 1000 )
  {
    delay( 500 );
    OutputStream->print( "." );
  }

  if( WiFi.status() == WL_CONNECTED )
  {
    OutputStream->println( " Connected");
    OutputStream->print( "IP address: " );
    OutputStream->println( WiFi.localIP() );
  }
  else
    OutputStream->println( " WiFi timed out" );


  http_setup();

  //At this point switch comms over to telnet
  TelnetStream2.begin();
  OutputStream->println( "Telnet Initalized");
  OutputStream->println( "CLI active");

  //Start Thread for Basic interpreter/FastLED here
  //setup_basic();
  //OutputStream->println( "BASIC task active");

#ifdef USE_TELNET
  OutputStream->println( "CLI now via Telnet. Press any key to return to Serial");
  setCLIEcho(false);
  OutputStream = &TelnetStream2;
#endif
  //Init command Line interpreter
  init_commands(OutputStream);
}


void loop()
{
  http_loop();

  //Run Shell commands and check serial port. Protected bymutex.
  //take_terminal();
  run_commands();
  check_serial();
  //give_terminal();

  //Check for startup.bas and if it exist run it once
  //basic_autoexec();

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

  // Process GPS messages
  gps_loop();

  //RUN Speed-Of-SOuind Effect
  //CIRCLE_effect();
  SOS_effect2();
}
