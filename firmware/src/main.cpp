
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
#include <HardwareSerial.h>
#include <RadioLib.h>
#include <esp_wifi.h>
#include <TelnetStream2.h>
#include "basic_wrapper.h"


#define USE_TELNET


#define FSLINK LittleFS
#include "commands.h"

Stream *OutputStream = NULL;


//I2C speed
#define I2C_FREQ      100000 // 400 kHz fast-mode; drop to 100 k if marginal

// Serial
HardwareSerial GPSSerial(0);


SPIClass spiLoRa( HSPI );
SPISettings spiLoRaSettings( 1000000, MSBFIRST, SPI_MODE0 );
SX1268 radio = new Module( LORA_PIN_CS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY, spiLoRa, spiLoRaSettings );

// Default LoRa parameters
//#define DOWNSTREAM_FREQUENCY    431.250         // 431.250MHz
//#define DOWNSTREAM_BANDWIDTH    500.0           // 500kHz
//#define DOWNSTREAM_SF           9               // SF7...SF12
//#define DOWNSTREAM_CR           6               // 4/6 coding rate
//#define DOWNSTREAM_PREAMBLE     8               // 8 preamble symbols
//#define DOWNSTREAM_TXPOWER      5               // Transmit power
//#define LORA_SYNC_WORD          0xDEAD

#define DOWNSTREAM_FREQUENCY    433.775         // 431.250MHz
#define DOWNSTREAM_BANDWIDTH    125.0           // 500kHz
#define DOWNSTREAM_SF           12               // SF7...SF12
#define DOWNSTREAM_CR           5               // 4/6 coding rate
#define DOWNSTREAM_PREAMBLE     8               // 8 preamble symbols
#define DOWNSTREAM_TXPOWER      5               // Transmit power
#define LORA_SYNC_WORD          0x1424


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
WebServer server(80);
static char hostname[17];


String html_escape( const char* str )
{
  String escaped;
  while (*str) {
    if (*str == '<') escaped += "&lt;";
    else if (*str == '>') escaped += "&gt;";
    else if (*str == '&') escaped += "&amp;";
    else escaped += *str;
    str++;
  }
  return escaped;
}


String getPartitionInfoHTML() {
  String out = "<h3>Firmware Versions in Partitions</h3><pre>";

  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t* part = esp_partition_get(it);
    esp_app_desc_t desc;
    bool hasInfo = esp_ota_get_partition_description(part, &desc) == ESP_OK;

    out += String(part->label) + " @ 0x" + String(part->address, HEX);
    out += " size 0x" + String(part->size, HEX);

    if (part == running) out += " [RUNNING]";
    if (part == boot)    out += " [BOOT]";

    if (hasInfo) {
      out += "\n  Version: ";
      out += html_escape(desc.version);
      out += "\n  Project: ";
      out += html_escape(desc.project_name);
      out += "\n  Built: ";
      out += String(desc.date) + " " + desc.time;
    } else {
      out += "\n  <i>No descriptor info</i>";
    }

    out += "\n\n";
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  out += "</pre>";
  return out;
}


void http_root()
{
  String page = "<html><body>";

  page += getPartitionInfoHTML();

  page += "<hr><br>\n";
  page += "<a href='/dir'>List Files</a><br>\n";
  page += "<a href='/nvs'>List NVS Parameters</a><br><br>\n";
  page += "<a href='/update'>Update Firmware</a><br>\n";
  //page += "<form method='POST' action='/reboot'><input type='submit' value='Reboot'></form>";
  page += "<a href='/reboot'>Reboot</a><br>\n";
  page += "</body></html>\n";
  server.send(200, "text/html", page);
}


void http_reboot()
{
  server.send(200, "text/plain", "Rebooting...");
  delay(1000);
  ESP.restart();
}


void http_dir()
{
  server.send( 200, "text/plain", "FIXME..." );
}


void http_nvs()
{
  server.send( 200, "text/plain", "FIXME..." );
}


void dump_partitions()
{
  const esp_partition_t* running = esp_ota_get_running_partition();
  const esp_partition_t* boot = esp_ota_get_boot_partition();

  Serial.println( "---- Partition Table ----" );

  esp_partition_iterator_t it;
  const esp_partition_t* part;

  it = esp_partition_find( ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL );
  while( it != NULL )
  {
    part = esp_partition_get( it );
    Serial.printf( "Partition: %-16s  Offset: 0x%08X  Size: 0x%06X (%u KB)  Type: 0x%02X/0x%02X",
                   part->label, part->address, part->size, part->size / 1024, part->type, part->subtype );

    if( part == running ) Serial.print( " [RUNNING]" );
    if( part == boot )    Serial.print( " [BOOT]" );

    Serial.println();
    it = esp_partition_next( it );
  }
  esp_partition_iterator_release( it );

  Serial.println( "" );

  // Print boot partition if it's not part of the APP partitions
  if( boot && running && boot != running )
  {
    Serial.printf( "Boot partition is different from currently running:\n  BOOT: %s at 0x%08X\n  RUNNING: %s at 0x%08X\n",
                   boot->label, boot->address, running->label, running->address );
  }
}


void dump_nvs() {
  Serial.println("---- NVS Parameters ----");

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.println("NVS init failed, erasing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  }

  nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY);

  if (it == NULL) {
    Serial.println("No NVS entries found.");
    return;
  }

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);

    Serial.printf("Namespace: %-12s Key: %-16s Type: ", info.namespace_name, info.key);

    nvs_handle_t handle;
    if (nvs_open(info.namespace_name, NVS_READONLY, &handle) != ESP_OK) {
      Serial.println("  [Failed to open namespace]");
      it = nvs_entry_next(it);
      continue;
    }

    switch (info.type) {
      case NVS_TYPE_I8: {
        int8_t val;
        nvs_get_i8(handle, info.key, &val);
        Serial.printf("int8_t    = %d\n", val);
        break;
      }
      case NVS_TYPE_U8: {
        uint8_t val;
        nvs_get_u8(handle, info.key, &val);
        Serial.printf("uint8_t   = %u\n", val);
        break;
      }
      case NVS_TYPE_I16: {
        int16_t val;
        nvs_get_i16(handle, info.key, &val);
        Serial.printf("int16_t   = %d\n", val);
        break;
      }
      case NVS_TYPE_U16: {
        uint16_t val;
        nvs_get_u16(handle, info.key, &val);
        Serial.printf("uint16_t  = %u\n", val);
        break;
      }
      case NVS_TYPE_I32: {
        int32_t val;
        nvs_get_i32(handle, info.key, &val);
        Serial.printf("int32_t   = %ld\n", val);
        break;
      }
      case NVS_TYPE_U32: {
        uint32_t val;
        nvs_get_u32(handle, info.key, &val);
        Serial.printf("uint32_t  = %lu\n", val);
        break;
      }
      case NVS_TYPE_I64: {
        int64_t val;
        nvs_get_i64(handle, info.key, &val);
        Serial.printf("int64_t   = %lld\n", val);
        break;
      }
      case NVS_TYPE_U64: {
        uint64_t val;
        nvs_get_u64(handle, info.key, &val);
        Serial.printf("uint64_t  = %llu\n", val);
        break;
      }
      case NVS_TYPE_STR: {
        size_t len = 0;
        nvs_get_str(handle, info.key, NULL, &len);
        char *str = (char *)malloc(len);
        if (str && nvs_get_str(handle, info.key, str, &len) == ESP_OK) {
          Serial.printf("string    = \"%s\"\n", str);
        } else {
          Serial.println("string    = [error reading]");
        }
        free(str);
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t len = 0;
        nvs_get_blob(handle, info.key, NULL, &len);
        uint8_t *blob = (uint8_t *)malloc(len);
        if (blob && nvs_get_blob(handle, info.key, blob, &len) == ESP_OK) {
          Serial.printf("blob[%u]  = ", (unsigned)len);
          for (size_t i = 0; i < len; i++) {
            Serial.printf("%02X ", blob[i]);
          }
          Serial.println();
        } else {
          Serial.println("blob      = [error reading]");
        }
        free(blob);
        break;
      }
      default:
        Serial.println("unknown   = [unsupported type]");
        break;
    }

    nvs_close(handle);
    it = nvs_entry_next(it);
  }

  Serial.println("");
}


void print_nvs_stats() {
  nvs_stats_t stats;
  esp_err_t err = nvs_get_stats(NVS_DEFAULT_PART_NAME, &stats);

  if (err != ESP_OK) {
    Serial.printf("Failed to get NVS stats: %s\n", esp_err_to_name(err));
    return;
  }

  Serial.println("NVS Usage Statistics:");
  Serial.printf("  Used entries     : %d\n", stats.used_entries);
  Serial.printf("  Free entries     : %d\n", stats.free_entries);
  Serial.printf("  Total entries    : %d\n", stats.total_entries);
  Serial.printf("  Namespace count  : %d\n", stats.namespace_count);
  Serial.println( "" );
}


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


// Blink an error code forever
void blinkloop( int flashes )
{
  int i;

  while( 1 )
  {
    for( i = 0; i < flashes; ++i )
    {
      digitalWrite( LED_PIN, HIGH );
      delay( 250 );
      digitalWrite( LED_PIN, LOW );
      delay( 250 );
    }

    digitalWrite( LED_PIN, LOW );

    delay( 1000 );

    Serial.print( "." );
  }
}


// IRQ handler for LoRa RX
volatile bool lora_rxdone_flag = false;
void IRAM_ATTR lora_rxdone( void )
{
  lora_rxdone_flag = true;
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


void hexdump( uint8_t *buf, int len )
{
  int i;

  if( len < 1 )
    return;

  for( i = 0; i < len; ++i )
  {
    OutputStream->print( buf[ i ], HEX );
    OutputStream->print( " " );
  }

  OutputStream->print( "\n" );
}


void lora_rx( void )
{
  unsigned int len;
  uint8_t buf[256];
  int status;
  float RSSI;
  float SNR;

  if( !lora_rxdone_flag )
    return;

  lora_rxdone_flag = false;

  OutputStream->print( "\nWe have RX flag!\n" );
  OutputStream->print( "radio.available = " );
  OutputStream->println( radio.available() );
  OutputStream->print( "radio.getRSSI = " );
  OutputStream->println( radio.getRSSI() );
  OutputStream->print( "radio.getSNR = " );
  OutputStream->println( radio.getSNR() );
  OutputStream->print( "radio.getPacketLength = " );
  OutputStream->println( radio.getPacketLength() );
  
  String str;
  int16_t state = radio.readData( str );

  if( state == RADIOLIB_ERR_NONE )
  {
    OutputStream->print( "Packet: " );
    OutputStream->println( str );
    hexdump( (uint8_t*)str.c_str(), str.length() );
  }

  OutputStream->print( "\n" );
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
  FastLED.addLeds<WS2811, RGB2_PIN, RGB>(leds3, NUM_LEDS3);
  FastLED.addLeds<WS2811, RGB2_PIN, RGB>(leds4, NUM_LEDS4);
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


  // Config SPI for LoRa radio.
  spiLoRa.begin( LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS );

  // Fire up the LoRa radio.
  OutputStream->print( "\nInit LoRa... " );

  radio.setTCXO( 1.8, 5000 );
  radio.setDio2AsRfSwitch();

  //int status = radio.begin( DOWNSTREAM_FREQUENCY, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_SF, DOWNSTREAM_CR, RADIOLIB_SX126X_SYNC_WORD_PRIVATE, DOWNSTREAM_TXPOWER, DOWNSTREAM_PREAMBLE );
  int status = radio.begin( DOWNSTREAM_FREQUENCY );

  if( status != RADIOLIB_ERR_NONE )
  {
    OutputStream->print( "Failed, status=" );
    OutputStream->println( status );

    //while( 1 )
    //  delay( 1 );   // Dead in the water
    blinkloop( 3 );
  }

  OutputStream->print( "OK\n" );

  // Set misc LoRa parameters.
  //radio.setSyncWord( 0xDE, 0xAD );
  radio.setSpreadingFactor( DOWNSTREAM_SF );
  radio.setBandwidth( DOWNSTREAM_BANDWIDTH );
  radio.setCodingRate( DOWNSTREAM_CR );
  radio.setPreambleLength( DOWNSTREAM_PREAMBLE );
  radio.setSyncWord( 0x12 );
  radio.setCRC( true );

  radio.setDio1Action( lora_rxdone );
  status = radio.startReceive();

  if( status == RADIOLIB_ERR_NONE )
  {
    OutputStream->print( "LoRa set to receive mode.\n" );
  }
  else
  {
    OutputStream->printf( "Failed to set LoRa to receive mode, status=%d\n", status );
  }

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

  server.on( "/", http_root );
  server.on( "/reboot", http_reboot );
  server.on( "/dir", http_dir );
  server.on( "/nvs", http_nvs );
  ElegantOTA.begin( &server );
  server.begin();

  GPSSerial.begin( 9600, SERIAL_8N1,     // baud, mode, RX-pin, TX-pin
                  44 /*RX0*/, 43 /*TX0*/ );

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
  server.handleClient();

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

