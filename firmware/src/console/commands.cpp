#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include "shell.h"
#include <WiFi.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "soc/io_mux_reg.h"
#include "soc/gpio_reg.h"
#include "esp_ota_ops.h"
#include <esp_app_format.h>
#include "basic_wrapper.h"
#ifdef INCLUDE_WASM
#include "wasm_wrapper.h"
#endif
#include "main.h"
#include "task.h"
#include "printManager.h"
#include "gps.h"
#include "sensors.h"
#include "lora.h"
#include "led.h"
#include "config.h"
#include "cue.h"
#include "psram.h"


//Serial/Telnet Shell comamnds

void renameFile(fs::FS &fs, const char *path1, const char *path2) 
{
    printfnl(SOURCE_COMMANDS, F("Renaming file %s to %s\r\n"), path1, path2);
    if (fs.rename(path1, path2)) {
      printfnl(SOURCE_COMMANDS, F("- file renamed\n") );
    } else {
      printfnl(SOURCE_COMMANDS, F("- rename failed\n") );
    }
}
  
void deleteFile(fs::FS &fs, const char *path)
{
    printfnl(SOURCE_COMMANDS, F("Deleting file: %s\r\n"), path);
    if (fs.remove(path)) {
      printfnl(SOURCE_COMMANDS, F( "- file deleted\n") );
    } else {
      printfnl(SOURCE_COMMANDS, F( "- delete failed\n") );
    }
}


void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  printfnl(SOURCE_COMMANDS, F("Listing directory: %s\r\n"), dirname);

  File root = fs.open(dirname);
  if (!root) {
    printfnl(SOURCE_COMMANDS, F("- failed to open directory\n") );
    return;
  }
  if (!root.isDirectory()) {
    printfnl(SOURCE_COMMANDS, F(" - not a directory\n") );
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      printfnl(SOURCE_COMMANDS, F("  DIR : %s\n"), file.name());
      if (levels) {
        listDir(fs, file.path(), levels - 1);
      }
    } else {
      printfnl(SOURCE_COMMANDS, F("  FILE: %s \tSIZE: %d\n"), file.name(),file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path) 
{
    printfnl(SOURCE_COMMANDS,"Listing file: %s\r\n\n", path);
  
    File file = fs.open(path);
    if (!file || file.isDirectory()) 
    {
      printfnl(SOURCE_COMMANDS, F("- failed to open file for reading\n") );
      return;
    }
  
    char buf[128];
    while (file.available())
    {
      int len = file.readBytesUntil('\n', buf, sizeof(buf) - 1);
      buf[len] = '\0';
      printfnl(SOURCE_COMMANDS, "%s\n", buf);
    }
    printfnl(SOURCE_COMMANDS, F("\n") );
    printfnl(SOURCE_COMMANDS, F("- file read complete\n") );
    file.close();
  }
  
  void writeFile(fs::FS &fs, const char *path, const char *message) 
  {
    printfnl(SOURCE_COMMANDS, F("Writing file: %s\r\n"), path);
  
    File file = fs.open(path, FILE_WRITE);
    if (!file) {
      printfnl(SOURCE_COMMANDS, F("- failed to open file for writing\n") );
      return;
    }
    if (file.print(message)) 
    {
      printfnl(SOURCE_COMMANDS, F("- file written\n") );
    } 
    else 
    {
      printfnl(SOURCE_COMMANDS, F("- write failed\n") );
    }
    file.close();
  }

/*
Commands
*/
int test(int argc, char **argv) 
{
  printfnl(SOURCE_COMMANDS, F("Test function called with %d Arguments\n"), argc);
  printfnl(SOURCE_COMMANDS, F(" Arguments:\n") );
  for (int ii=0;ii<argc;ii++)
  {
    printfnl(SOURCE_COMMANDS, F("Argument %d: %s\n"), ii, argv[ii]);
  }  
  return 0;
};


int cmd_reboot( int argc, char **argv )
{
    printfnl( SOURCE_SYSTEM, F("Rebooting...\n") );
    delay( 1000 );
    ESP.restart();

    return 0;
}


int cmd_debug( int argc, char **argv )
{
    // If no args, show current debug message config.
    if( argc < 2 )
    {
        printfnl(SOURCE_COMMANDS, F("Current Debug Settings:\n") );

        printfnl(SOURCE_COMMANDS, F(" - SYSTEM: \t%s\n"), getDebug(SOURCE_SYSTEM) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - BASIC: \t%s\n"), getDebug(SOURCE_BASIC) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - WASM: \t%s\n"), getDebug(SOURCE_WASM) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - COMMANDS: \t%s\n"), getDebug(SOURCE_COMMANDS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - SHELL: \t%s\n"), getDebug(SOURCE_SHELL) ? "on" : "off" );        
        printfnl(SOURCE_COMMANDS, F(" - GPS: \t%s\n"), getDebug(SOURCE_GPS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - GPS_RAW: \t%s\n"), getDebug(SOURCE_GPS_RAW) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - LORA: \t%s\n"), getDebug(SOURCE_LORA) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - LORA_RAW: \t%s\n"), getDebug(SOURCE_LORA_RAW) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - FSYNC: \t%s\n"), getDebug(SOURCE_FSYNC) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - WIFI: \t%s\n"), getDebug(SOURCE_WIFI) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - SENSORS: \t%s\n"), getDebug(SOURCE_SENSORS) ? "on" : "off" );
        printfnl(SOURCE_COMMANDS, F(" - OTHER: \t%s\n"), getDebug(SOURCE_OTHER) ? "on" : "off" );

        return 0;
    }

    // Turn off all debug messages?
    if( !strcasecmp( argv[1], "off" ) )
    {
        setDebugOff();
        return 0;
    }

    uint32_t mask_to_set = 0;
    if( !strcasecmp( argv[1], "SYSTEM" ) )
        mask_to_set = SOURCE_SYSTEM;
    else
    if( !strcasecmp( argv[1], "BASIC" ) )
        mask_to_set = SOURCE_BASIC;
    else
    if( !strcasecmp( argv[1], "WASM" ) )
        mask_to_set = SOURCE_WASM;
    else
    if( !strcasecmp( argv[1], "COMMANDS" ) )
        mask_to_set = SOURCE_COMMANDS;
    else
    if( !strcasecmp( argv[1], "SHELL" ) )
        mask_to_set = SOURCE_SHELL;
    else
    if( !strcasecmp( argv[1], "GPS" ) )
        mask_to_set = SOURCE_GPS;
    else
    if( !strcasecmp( argv[1], "GPS_RAW" ) )
        mask_to_set = SOURCE_GPS_RAW;
    else
    if( !strcasecmp( argv[1], "LORA" ) )
        mask_to_set = SOURCE_LORA;
    else
    if( !strcasecmp( argv[1], "LORA_RAW" ) )
        mask_to_set = SOURCE_LORA_RAW;
    else
    if( !strcasecmp( argv[1], "WIFI" ) )
        mask_to_set = SOURCE_WIFI;
    else
    if( !strcasecmp( argv[1], "FSYNC" ) )
        mask_to_set = SOURCE_FSYNC;
    else
    if( !strcasecmp( argv[1], "OTHER" ) )
        mask_to_set = SOURCE_OTHER;
    else       
    if( !strcasecmp( argv[1], "SENSORS" ) )
        mask_to_set = SOURCE_SENSORS;
    else            
    {
        printfnl(SOURCE_COMMANDS, F("Debug name \"%s\"not recognized.\n"), argv[1] );
        return 1;
    }

    // If someone just does "debug {name}", treat the same as "debug {name} on"
    if( argc == 2 )
        setDebugLevel( (source_e) mask_to_set, true );
    else
    if( argc >= 3 )
    {
        if( !strcasecmp( argv[2], "off" ) )
            setDebugLevel((source_e)mask_to_set, false);
        else
            setDebugLevel((source_e)mask_to_set, true);
    }
    
    return 0;
}

int delFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    deleteFile(LittleFS,argv[1]);
    return 0;
}

int renFile(int argc, char **argv) 
{
    if (argc != 3)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    renameFile(LittleFS,argv[1], argv[2]);
    return 0;
}

int listFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }

    readFile(LittleFS,argv[1]); 
    printfnl(SOURCE_COMMANDS, F("\n"));
    return 0;
}

int listDir(int argc, char **argv) 
{
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    listDir(LittleFS,"/",1); 
    return 0;
}

int loadFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;       
    }
    else
    {
        int linecount = 0;
        int charcount = 0;
        char line[256];
        char inchar;
        bool isDone = false;
        printfnl(SOURCE_COMMANDS, F("Ready for file. Press CTRL+Z to end transmission and save file %s\n"), argv[1]);
        //Flush serial buffer
        getLock();
        getStream()->flush();
        //create file
        File file = LittleFS.open(argv[1], FILE_WRITE);
        if (!file)
        {
            releaseLock();
            printfnl(SOURCE_COMMANDS, F("- failed to open file for writing\n") );
            return 1;
        }

        do
        {
            //Get one character from serial port
            if (getStream()->available())
            {
                inchar = getStream()->read();
                //Check if its a break character
                if (inchar == 0x1A) 
                {
                    //Break loop 
                    break;
                }
                else
                {
                    //Wait for a full line
                    line[charcount] = inchar;
                    charcount++;
                    if (charcount>254)
                    {
                        getStream()->printf("Line %d too long\n",linecount+1);
                        break;
                    }
                    if (inchar == '\n')
                    {
                        //Write line
                        line[charcount] = '\0';
                        if (file.print(line))
                        {
                        } 
                        else 
                        {
                          getStream()->printf("Write Error\n");
                          file.close();
                          releaseLock();
                          return 1;
                        }
                        //increase line counter
                        linecount++;
                        //clear line
                        charcount = 0;
                        line[0] = 0;
            
                    }
                }
            }
        }
        while (isDone == false);
        //close file
        file.close();
        releaseLock();
        printfnl(SOURCE_COMMANDS, F("%d Lines written to file\n"), linecount);
        
        return 0;
    }

}

int runBasic(int argc, char **argv)
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, F("Usage: run <file.bas|file.wasm>\n") );
        return 1;
    }
    else
    {
        if (false == set_script_program(argv[1]))
          printfnl(SOURCE_COMMANDS, F("Script already running or unknown type\n") );
        return 0;
    }
}

int stopBasic(int argc, char **argv)
{
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    else
    {
        set_basic_param(0,1);
#ifdef INCLUDE_WASM
        wasm_request_stop();
#endif
        return 0;
    }
}

int paramBasic(int argc, char **argv) 
{
    if (argc != 3)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;       
    }
    else
    {
        set_basic_param(atoi(argv[1]),atoi(argv[2]));      
        return 0;
    }
}

int cmd_mem(int argc, char **argv)
{
    printfnl(SOURCE_COMMANDS, F("Heap:\n") );
    printfnl(SOURCE_COMMANDS, F("  Free:    %u bytes\n"), esp_get_free_heap_size() );
    printfnl(SOURCE_COMMANDS, F("  Min:     %u bytes  (lowest since boot)\n"), esp_get_minimum_free_heap_size() );
    printfnl(SOURCE_COMMANDS, F("  Largest: %u bytes  (biggest allocatable block)\n"), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) );

    printfnl(SOURCE_COMMANDS, F("\nPSRAM:\n") );
    if (psram_available()) {
        printfnl(SOURCE_COMMANDS, F("  Size:       %u bytes (%u KB)\n"), psram_size(), psram_size()/1024);
        printfnl(SOURCE_COMMANDS, F("  Used:       %u bytes\n"), psram_bytes_used());
        printfnl(SOURCE_COMMANDS, F("  Free:       %u bytes\n"), psram_bytes_free());
        printfnl(SOURCE_COMMANDS, F("  Contiguous: %u bytes\n"), psram_bytes_contiguous());
        printfnl(SOURCE_COMMANDS, F("  Alloc slots: %d / %d\n"), psram_alloc_count(), psram_alloc_entries_max());
    } else {
        printfnl(SOURCE_COMMANDS, F("  Not available (using heap fallback)\n") );
    }

    return 0;
}


int cmd_ps(int argc, char **argv)
{
    // Known task names (application + typical Arduino/ESP-IDF system tasks)
    static const char *taskNames[] = {
        "loopTask",     // Arduino main loop
        "BasicTask",    // BASIC interpreter
        "WasmTask",     // WASM runtime
        "led_render",   // LED render task
        "IDLE0",        // Idle task core 0
        "IDLE1",        // Idle task core 1
        "Tmr Svc",      // FreeRTOS timer service
        "async_tcp",    // Async TCP (if WiFi active)
        "wifi",         // WiFi task
        "tiT",          // TCP/IP task
        "sys_evt",      // System event task
        "arduino_events", // Arduino event loop
    };
    static const int numNames = sizeof(taskNames) / sizeof(taskNames[0]);

    printfnl(SOURCE_COMMANDS, F("Task List (%u total tasks):\n"), (unsigned int)uxTaskGetNumberOfTasks() );
    printfnl(SOURCE_COMMANDS, F("  %-16s %-6s %4s  %4s  %s\n"), "Name", "State", "Prio", "Core", "Min Free Stack" );

    for (int i = 0; i < numNames; i++)
    {
        TaskHandle_t handle = xTaskGetHandle(taskNames[i]);
        if (handle == NULL)
            continue;

        const char *state;
        switch (eTaskGetState(handle))
        {
            case eRunning:   state = "Run";   break;
            case eReady:     state = "Ready"; break;
            case eBlocked:   state = "Block"; break;
            case eSuspended: state = "Susp";  break;
            case eDeleted:   state = "Del";   break;
            default:         state = "?";     break;
        }

        UBaseType_t prio = uxTaskPriorityGet(handle);
        BaseType_t coreId = xTaskGetAffinity(handle);
        uint32_t freeStackBytes = (uint32_t)uxTaskGetStackHighWaterMark(handle) * 4;

        if (coreId == tskNO_AFFINITY)
            printfnl(SOURCE_COMMANDS, F("  %-16s %-6s %4u     -  %u\n"),
                taskNames[i], state,
                (unsigned int)prio,
                (unsigned int)freeStackBytes );
        else
            printfnl(SOURCE_COMMANDS, F("  %-16s %-6s %4u  %4d  %u\n"),
                taskNames[i], state,
                (unsigned int)prio,
                (int)coreId,
                (unsigned int)freeStackBytes );
    }

    return 0;
}


int cmd_uptime(int argc, char **argv)
{
    unsigned long ms = millis();
    unsigned long totalSec = ms / 1000;
    unsigned int days  = totalSec / 86400;
    unsigned int hours = (totalSec % 86400) / 3600;
    unsigned int mins  = (totalSec % 3600) / 60;
    unsigned int secs  = totalSec % 60;
    printfnl(SOURCE_COMMANDS, F("Uptime: %ud %02uh %02um %02us\n"), days, hours, mins, secs );
    return 0;
}


int tc(int argc, char **argv)
{
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    else
    {
        printfnl(SOURCE_COMMANDS,F("Thread Count:\n") );
        for (int ii=0;ii<4;ii++)
        {
            printfnl( SOURCE_COMMANDS, F("Core %d: %d\n"), (uint8_t)ii, (unsigned int)get_thread_count(ii) );
        }
        return 0;
    }
}


int cmd_version(int argc, char **argv)
{
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_app_desc_t desc;

    if (running && esp_ota_get_partition_description(running, &desc) == ESP_OK)
    {
        printfnl(SOURCE_COMMANDS, F("Firmware: %s\n"), desc.project_name);
        printfnl(SOURCE_COMMANDS, F("Version: %s\n"), desc.version);
        printfnl(SOURCE_COMMANDS, F("Built:   %s %s\n"), desc.date, desc.time);
    }
    else
    {
        printfnl(SOURCE_COMMANDS, F("Firmware info unavailable\n"));
    }

#ifdef BOARD_CONEZ_V0_1
    printfnl(SOURCE_COMMANDS, F("Board:   conez-v0-1\n"));
#elif defined(BOARD_HELTEC_LORA32_V3)
    printfnl(SOURCE_COMMANDS, F("Board:   heltec-lora32-v3\n"));
#else
    printfnl(SOURCE_COMMANDS, F("Board:   unknown\n"));
#endif

    return 0;
}


int cmd_wifi(int argc, char **argv)
{
    printfnl(SOURCE_COMMANDS, F("WiFi Status:\n"));

    if (WiFi.status() == WL_CONNECTED)
    {
        printfnl(SOURCE_COMMANDS, F("  SSID:   %s\n"), WiFi.SSID().c_str());
        printfnl(SOURCE_COMMANDS, F("  Status: Connected\n"));
        printfnl(SOURCE_COMMANDS, F("  IP:     %s\n"), WiFi.localIP().toString().c_str());
        printfnl(SOURCE_COMMANDS, F("  RSSI:   %d dBm\n"), WiFi.RSSI());
    }
    else
    {
        printfnl(SOURCE_COMMANDS, F("  Status: Disconnected\n"));
    }

    return 0;
}


// --- GPIO pin name table (board-specific) ---
struct PinInfo { int pin; const char *name; };

static const PinInfo pin_table[] = {
#ifdef BOARD_CONEZ_V0_1
    {  0, "BOOT/USR" },
    {  1, "ADC_BAT" },
    {  2, "ADC_SOLAR" },
    {  3, "(reserved)" },
    {  4, "PSR_MISO" },
    {  5, "PSR_CE" },
    {  6, "PSR_SCK" },
    {  7, "PSR_MOSI" },
    {  8, "LORA_CS" },
    {  9, "LORA_SCK" },
    { 10, "LORA_MOSI" },
    { 11, "LORA_MISO" },
    { 12, "LORA_RST" },
    { 13, "LORA_BUSY" },
    { 14, "LORA_DIO1" },
    { 15, "EXT1" },
    { 16, "EXT2" },
    { 17, "I2C_SDA" },
    { 18, "I2C_SCL" },
    { 19, "USB_N" },
    { 20, "USB_P" },
    { 21, "SOLAR_PWM" },
    { 33, "PWR_SW" },
    { 34, "PWR_OFF" },
    { 35, "RGB4" },
    { 36, "RGB3" },
    { 37, "RGB2" },
    { 38, "RGB1" },
    { 40, "LED" },
    { 41, "IMU_INT" },
    { 42, "GPS_PPS" },
    { 43, "GPS_TX" },
    { 44, "GPS_RX" },
    { 47, "LOAD_ON" },
    { 48, "BUZZER" },
#elif defined(BOARD_HELTEC_LORA32_V3)
    {  0, "BUTTON" },
    {  1, "ADC_BAT" },
    {  8, "LORA_CS" },
    {  9, "LORA_SCK" },
    { 10, "LORA_MOSI" },
    { 11, "LORA_MISO" },
    { 12, "LORA_RST" },
    { 13, "LORA_BUSY" },
    { 14, "LORA_DIO1" },
    { 17, "I2C_SDA" },
    { 18, "I2C_SCL" },
    { 19, "USB_N" },
    { 20, "USB_P" },
    { 21, "OLED_RST" },
    { 35, "LED" },
    { 36, "VEXT" },
    { 43, "USB_TX" },
    { 44, "USB_RX" },
#endif
    { -1, NULL }
};

static const char *pin_name_lookup(int gpio)
{
    for (const PinInfo *p = pin_table; p->pin >= 0; p++)
        if (p->pin == gpio) return p->name;
    return "";
}

// Returns true if the pin is a valid ESP32-S3 GPIO (0-21, 33-48)
static bool gpio_valid_pin(int pin)
{
    return (pin >= 0 && pin <= 21) || (pin >= 33 && pin <= 48);
}

// Returns true if the pin is reserved for critical hardware and should not be reconfigured
static bool gpio_is_reserved(int pin)
{
    // USB
    if (pin == 19 || pin == 20) return true;
#ifdef BOARD_CONEZ_V0_1
    // PSRAM SPI
    if (pin >= 4 && pin <= 7) return true;
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // GPS UART
    if (pin == 43 || pin == 44) return true;
    // GPS PPS
    if (pin == 42) return true;
    // I2C
    if (pin == 17 || pin == 18) return true;
#elif defined(BOARD_HELTEC_LORA32_V3)
    // LoRa SPI + control
    if (pin >= 8 && pin <= 14) return true;
    // I2C / OLED
    if (pin == 17 || pin == 18 || pin == 21) return true;
#endif
    return false;
}

static void gpio_show_all(void)
{
    printfnl(SOURCE_COMMANDS, F("GPIO  Val  Dir  Pull      Function\n"));
    printfnl(SOURCE_COMMANDS, F("----  ---  ---  --------  ----------\n"));

    uint32_t out_en_lo = REG_READ(GPIO_ENABLE_REG);
    uint32_t out_en_hi = REG_READ(GPIO_ENABLE1_REG);

    for (int i = 0; i <= 48; i++) {
        // ESP32-S3 has no GPIO 22-32
        if (i >= 22 && i <= 32) continue;

        int level = digitalRead(i);

        bool is_output;
        if (i < 32)
            is_output = (out_en_lo >> i) & 1;
        else
            is_output = (out_en_hi >> (i - 32)) & 1;

        uint32_t iomux_reg = REG_READ(GPIO_PIN_MUX_REG[i]);
        bool pull_up   = (iomux_reg >> 8) & 1;
        bool pull_down = (iomux_reg >> 7) & 1;

        const char *pull_str;
        if (pull_up && pull_down) pull_str = "UP+DOWN";
        else if (pull_up)        pull_str = "UP";
        else if (pull_down)      pull_str = "DOWN";
        else                     pull_str = "-";

        const char *name = pin_name_lookup(i);

        printfnl(SOURCE_COMMANDS, F(" %2d    %d   %s  %-8s  %s\n"),
            i, level,
            is_output ? "OUT" : "IN ",
            pull_str, name);
    }
}

int cmd_gpio(int argc, char **argv)
{
    // "gpio" — show all pin states
    if (argc == 1) {
        gpio_show_all();
        return 0;
    }

    // "gpio set <pin> <0|1>" — set output level
    if (argc == 4 && strcasecmp(argv[1], "set") == 0) {
        int pin = atoi(argv[2]);
        int val = atoi(argv[3]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, F("Invalid GPIO pin %d\n"), pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, F("GPIO %d is reserved (use 'gpio' to see pin assignments)\n"), pin);
            return -1;
        }
        if (val != 0 && val != 1) {
            printfnl(SOURCE_COMMANDS, F("Value must be 0 or 1\n"));
            return -1;
        }
        digitalWrite(pin, val);
        printfnl(SOURCE_COMMANDS, F("GPIO %d -> %d\n"), pin, val);
        return 0;
    }

    // "gpio out <pin> <0|1>" — configure as output and set value
    if (argc == 4 && strcasecmp(argv[1], "out") == 0) {
        int pin = atoi(argv[2]);
        int val = atoi(argv[3]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, F("Invalid GPIO pin %d\n"), pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, F("GPIO %d is reserved (use 'gpio' to see pin assignments)\n"), pin);
            return -1;
        }
        if (val != 0 && val != 1) {
            printfnl(SOURCE_COMMANDS, F("Value must be 0 or 1\n"));
            return -1;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val);
        printfnl(SOURCE_COMMANDS, F("GPIO %d -> OUTPUT %d\n"), pin, val);
        return 0;
    }

    // "gpio in <pin> [pull]" — configure as input with optional pull
    //   pull: up, down, none (default: none)
    if ((argc == 3 || argc == 4) && strcasecmp(argv[1], "in") == 0) {
        int pin = atoi(argv[2]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, F("Invalid GPIO pin %d\n"), pin);
            return -1;
        }
        if (gpio_is_reserved(pin)) {
            printfnl(SOURCE_COMMANDS, F("GPIO %d is reserved (use 'gpio' to see pin assignments)\n"), pin);
            return -1;
        }
        int mode = INPUT;
        const char *pull_name = "none";
        if (argc == 4) {
            if (strcasecmp(argv[3], "up") == 0) {
                mode = INPUT_PULLUP;
                pull_name = "pull-up";
            } else if (strcasecmp(argv[3], "down") == 0) {
                mode = INPUT_PULLDOWN;
                pull_name = "pull-down";
            } else if (strcasecmp(argv[3], "none") == 0) {
                mode = INPUT;
                pull_name = "none";
            } else {
                printfnl(SOURCE_COMMANDS, F("Pull mode must be: up, down, or none\n"));
                return -1;
            }
        }
        pinMode(pin, mode);
        printfnl(SOURCE_COMMANDS, F("GPIO %d -> INPUT (%s)\n"), pin, pull_name);
        return 0;
    }

    // "gpio read <pin>" — read a single pin
    if (argc == 3 && strcasecmp(argv[1], "read") == 0) {
        int pin = atoi(argv[2]);
        if (!gpio_valid_pin(pin)) {
            printfnl(SOURCE_COMMANDS, F("Invalid GPIO pin %d\n"), pin);
            return -1;
        }
        printfnl(SOURCE_COMMANDS, F("GPIO %d = %d\n"), pin, digitalRead(pin));
        return 0;
    }

    printfnl(SOURCE_COMMANDS, F("Usage:\n"));
    printfnl(SOURCE_COMMANDS, F("  gpio              Show all pin states\n"));
    printfnl(SOURCE_COMMANDS, F("  gpio set <pin> <0|1>      Set output level\n"));
    printfnl(SOURCE_COMMANDS, F("  gpio out <pin> <0|1>      Set as output with value\n"));
    printfnl(SOURCE_COMMANDS, F("  gpio in  <pin> [up|down|none]  Set as input\n"));
    printfnl(SOURCE_COMMANDS, F("  gpio read <pin>           Read single pin\n"));
    return -1;
}


static void gps_show_status(void)
{
#ifdef BOARD_HAS_GPS
    static const char *fix_names[] = { "Unknown", "No Fix", "2D", "3D" };
    int ft = get_fix_type();
    const char *fix_str = (ft >= 0 && ft <= 3) ? fix_names[ft] : "Unknown";
    printfnl(SOURCE_COMMANDS, F("GPS Status:\n"));
    printfnl(SOURCE_COMMANDS, F("  Fix:        %s (%s)\n"), get_gpsstatus() ? "Yes" : "No", fix_str);
    printfnl(SOURCE_COMMANDS, F("  Satellites: %d\n"), get_satellites());
    printfnl(SOURCE_COMMANDS, F("  HDOP:       %.2f\n"), get_hdop() / 100.0);
    printfnl(SOURCE_COMMANDS, F("  VDOP:       %.2f\n"), get_vdop());
    printfnl(SOURCE_COMMANDS, F("  PDOP:       %.2f\n"), get_pdop());
    printfnl(SOURCE_COMMANDS, F("  Position:   %.6f, %.6f\n"), get_lat(), get_lon());
    float alt_m = get_alt();
    printfnl(SOURCE_COMMANDS, F("  Altitude:   %.0f m (%.0f ft)\n"), alt_m, alt_m * 3.28084f);
    float spd_mps = get_speed();
    printfnl(SOURCE_COMMANDS, F("  Speed:      %.1f m/s (%.1f mph)\n"), spd_mps, spd_mps * 2.23694f);
    printfnl(SOURCE_COMMANDS, F("  Direction:  %.1f deg\n"), get_dir());
    printfnl(SOURCE_COMMANDS, F("  Time:       %02d:%02d:%02d  %04d-%02d-%02d\n"),
        get_hour(), get_minute(), get_second(),
        get_year(), get_month(), get_day());
    static const char *src_names[] = { "None", "NTP", "GPS+PPS" };
    uint8_t ts = get_time_source();
    printfnl(SOURCE_COMMANDS, F("  Time src:   %s\n"), src_names[ts < 3 ? ts : 0]);
    uint32_t pps_age = get_pps_age_ms();
    if (pps_age == UINT32_MAX)
        printfnl(SOURCE_COMMANDS, F("  PPS:        No (never received)\n"));
    else
        printfnl(SOURCE_COMMANDS, F("  PPS:        %s (%lu ms ago, %lu pulses)\n"),
            get_pps() ? "High" : "Low", (unsigned long)pps_age, (unsigned long)get_pps_count());
#else
    printfnl(SOURCE_COMMANDS, F("GPS not available on this board\n"));
#endif
}


static void gps_show_usage(void)
{
    printfnl(SOURCE_COMMANDS, F("Usage:\n"));
    printfnl(SOURCE_COMMANDS, F("  gps                        Show GPS status\n"));
    printfnl(SOURCE_COMMANDS, F("  gps info                   Query module firmware/hardware\n"));
    printfnl(SOURCE_COMMANDS, F("  gps set baud <rate>        Set baud (4800/9600/19200/38400/57600/115200)\n"));
    printfnl(SOURCE_COMMANDS, F("  gps set rate <hz>          Set update rate (1/2/4/5/10)\n"));
    printfnl(SOURCE_COMMANDS, F("  gps set mode <mode>        Set constellation (gps/bds/glonass or combos)\n"));
    printfnl(SOURCE_COMMANDS, F("  gps set nmea <sentences>   Enable NMEA sentences (e.g. gga,rmc,gsa)\n"));
    printfnl(SOURCE_COMMANDS, F("  gps save                   Save config to module flash\n"));
    printfnl(SOURCE_COMMANDS, F("  gps restart <type>         Restart (hot/warm/cold/factory)\n"));
    printfnl(SOURCE_COMMANDS, F("  gps send <body>            Send raw NMEA (auto-checksum)\n"));
}


int cmd_gps(int argc, char **argv)
{
    // No subcommand — show status
    if (argc < 2) {
        gps_show_status();
        return 0;
    }

#ifndef BOARD_HAS_GPS
    printfnl(SOURCE_COMMANDS, F("GPS not available on this board\n"));
    return -1;
#else

    // --- gps info: query module firmware and hardware ---
    if (strcasecmp(argv[1], "info") == 0) {
        printfnl(SOURCE_COMMANDS, F("Querying GPS module info (enable 'debug gps_raw' to see response)...\n"));
        gps_send_nmea("PCAS06,0");  // firmware version
        gps_send_nmea("PCAS06,1");  // hardware model
        return 0;
    }

    // --- gps set <subcommand> ---
    if (strcasecmp(argv[1], "set") == 0) {
        if (argc < 3) {
            gps_show_usage();
            return -1;
        }

        // --- gps set baud <rate> ---
        if (strcasecmp(argv[2], "baud") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, F("Usage: gps set baud <4800|9600|19200|38400|57600|115200>\n"));
                return -1;
            }
            int rate = atoi(argv[3]);
            int code = -1;
            switch (rate) {
                case 4800:   code = 0; break;
                case 9600:   code = 1; break;
                case 19200:  code = 2; break;
                case 38400:  code = 3; break;
                case 57600:  code = 4; break;
                case 115200: code = 5; break;
            }
            if (code < 0) {
                printfnl(SOURCE_COMMANDS, F("Invalid baud rate. Use: 4800/9600/19200/38400/57600/115200\n"));
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS01,%d", code);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, F("Baud set to %d (use 'gps save' to persist)\n"), rate);
            printfnl(SOURCE_COMMANDS, F("Note: firmware still expects 9600. Reboot to reconnect.\n"));
            return 0;
        }

        // --- gps set rate <hz> ---
        if (strcasecmp(argv[2], "rate") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, F("Usage: gps set rate <1|2|4|5|10>\n"));
                return -1;
            }
            int hz = atoi(argv[3]);
            int ms = -1;
            switch (hz) {
                case 1:  ms = 1000; break;
                case 2:  ms = 500;  break;
                case 4:  ms = 250;  break;
                case 5:  ms = 200;  break;
                case 10: ms = 100;  break;
            }
            if (ms < 0) {
                printfnl(SOURCE_COMMANDS, F("Invalid rate. Use: 1, 2, 4, 5, or 10 Hz\n"));
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS02,%d", ms);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, F("Update rate set to %d Hz (%d ms)\n"), hz, ms);
            return 0;
        }

        // --- gps set mode <constellation> ---
        if (strcasecmp(argv[2], "mode") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, F("Usage: gps set mode <gps|bds|glonass|gps+bds|gps+glonass|bds+glonass|all>\n"));
                return -1;
            }
            int mode = -1;
            if (strcasecmp(argv[3], "gps") == 0)           mode = 1;
            else if (strcasecmp(argv[3], "bds") == 0)       mode = 2;
            else if (strcasecmp(argv[3], "gps+bds") == 0)   mode = 3;
            else if (strcasecmp(argv[3], "glonass") == 0)    mode = 4;
            else if (strcasecmp(argv[3], "gps+glonass") == 0) mode = 5;
            else if (strcasecmp(argv[3], "bds+glonass") == 0) mode = 6;
            else if (strcasecmp(argv[3], "all") == 0)        mode = 7;

            if (mode < 0) {
                printfnl(SOURCE_COMMANDS, F("Invalid mode. Use: gps, bds, glonass, gps+bds, gps+glonass, bds+glonass, all\n"));
                return -1;
            }
            char buf[16];
            snprintf(buf, sizeof(buf), "PCAS04,%d", mode);
            gps_send_nmea(buf);
            printfnl(SOURCE_COMMANDS, F("Constellation mode set to %d\n"), mode);
            return 0;
        }

        // --- gps set nmea <sentences> ---
        // Enables listed NMEA sentences at 1Hz, disables the rest
        // e.g. "gps set nmea gga,rmc,gsa"
        if (strcasecmp(argv[2], "nmea") == 0) {
            if (argc < 4) {
                printfnl(SOURCE_COMMANDS, F("Usage: gps set nmea <gga,gll,gsa,gsv,rmc,vtg,zda,...>\n"));
                printfnl(SOURCE_COMMANDS, F("  Enables listed sentences at 1/fix, disables others\n"));
                printfnl(SOURCE_COMMANDS, F("  Slots: gga,gll,gsa,gsv,rmc,vtg,zda,ant,dhv,lps,,,utc,gst\n"));
                return -1;
            }
            // PCAS03 field order: GGA,GLL,GSA,GSV,RMC,VTG,ZDA,ANT,DHV,LPS,res,res,UTC,GST,res,res,res,TIM
            static const char *names[] = {
                "gga","gll","gsa","gsv","rmc","vtg","zda","ant","dhv","lps",
                NULL, NULL, "utc", "gst", NULL, NULL, NULL, "tim"
            };
            int fields[18] = {0};

            // Parse comma-separated list from argv[3]
            char list[64];
            strncpy(list, argv[3], sizeof(list) - 1);
            list[sizeof(list) - 1] = '\0';
            char *saveptr;
            char *tok = strtok_r(list, ",", &saveptr);
            while (tok) {
                bool found = false;
                for (int i = 0; i < 18; i++) {
                    if (names[i] && strcasecmp(tok, names[i]) == 0) {
                        fields[i] = 1;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    printfnl(SOURCE_COMMANDS, F("  Unknown sentence: %s (ignored)\n"), tok);
                tok = strtok_r(NULL, ",", &saveptr);
            }

            char buf[80];
            snprintf(buf, sizeof(buf),
                "PCAS03,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                fields[0], fields[1], fields[2], fields[3], fields[4], fields[5],
                fields[6], fields[7], fields[8], fields[9], fields[10], fields[11],
                fields[12], fields[13], fields[14], fields[15], fields[16], fields[17]);
            gps_send_nmea(buf);
            return 0;
        }

        gps_show_usage();
        return -1;
    }

    // --- gps save ---
    if (strcasecmp(argv[1], "save") == 0) {
        gps_send_nmea("PCAS00");
        printfnl(SOURCE_COMMANDS, F("Configuration saved to GPS module flash\n"));
        return 0;
    }

    // --- gps restart <type> ---
    if (strcasecmp(argv[1], "restart") == 0) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, F("Usage: gps restart <hot|warm|cold|factory>\n"));
            return -1;
        }
        int rs = -1;
        if (strcasecmp(argv[2], "hot") == 0)      rs = 0;
        else if (strcasecmp(argv[2], "warm") == 0)  rs = 1;
        else if (strcasecmp(argv[2], "cold") == 0)  rs = 2;
        else if (strcasecmp(argv[2], "factory") == 0) rs = 3;

        if (rs < 0) {
            printfnl(SOURCE_COMMANDS, F("Invalid restart type. Use: hot, warm, cold, factory\n"));
            return -1;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "PCAS10,%d", rs);
        gps_send_nmea(buf);
        printfnl(SOURCE_COMMANDS, F("GPS module restarting (%s)\n"), argv[2]);
        return 0;
    }

    // --- gps send <raw body> ---
    // Send arbitrary NMEA body with auto-checksum, e.g. "gps send PCAS06,0"
    if (strcasecmp(argv[1], "send") == 0) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, F("Usage: gps send <NMEA body>  (e.g. PCAS06,0)\n"));
            return -1;
        }
        // Rejoin remaining args with spaces (in case user typed spaces)
        char buf[80];
        buf[0] = '\0';
        for (int i = 2; i < argc; i++) {
            if (i > 2) strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, argv[i], sizeof(buf) - strlen(buf) - 1);
        }
        gps_send_nmea(buf);
        return 0;
    }

    gps_show_usage();
    return -1;
#endif
}


int cmd_lora(int argc, char **argv)
{
#ifdef BOARD_HAS_LORA
    printfnl(SOURCE_COMMANDS, F("LoRa Radio:\n"));
    printfnl(SOURCE_COMMANDS, F("  Frequency: %.3f MHz\n"), lora_get_frequency());
    printfnl(SOURCE_COMMANDS, F("  Bandwidth: %.1f kHz\n"), lora_get_bandwidth());
    printfnl(SOURCE_COMMANDS, F("  SF:        %d\n"), lora_get_sf());
    printfnl(SOURCE_COMMANDS, F("  Last RSSI: %.1f dBm\n"), lora_get_rssi());
    printfnl(SOURCE_COMMANDS, F("  Last SNR:  %.1f dB\n"), lora_get_snr());
#else
    printfnl(SOURCE_COMMANDS, F("LoRa not available on this board\n"));
#endif
    return 0;
}


int cmd_sensors(int argc, char **argv)
{
    printfnl(SOURCE_COMMANDS, F("Sensors:\n"));

#ifdef BOARD_HAS_IMU
    printfnl(SOURCE_COMMANDS, F("  IMU:         %s\n"), imuAvailable() ? "Available" : "Not detected");
    if (imuAvailable())
    {
        printfnl(SOURCE_COMMANDS, F("  Roll:        %.1f deg\n"), getRoll());
        printfnl(SOURCE_COMMANDS, F("  Pitch:       %.1f deg\n"), getPitch());
        printfnl(SOURCE_COMMANDS, F("  Yaw:         %.1f deg\n"), getYaw());
        printfnl(SOURCE_COMMANDS, F("  Accel:       %.2f, %.2f, %.2f g\n"), getAccX(), getAccY(), getAccZ());
    }
#else
    printfnl(SOURCE_COMMANDS, F("  IMU:         Not available on this board\n"));
#endif

    printfnl(SOURCE_COMMANDS, F("  Temperature: %.1f C\n"), getTemp());
    printfnl(SOURCE_COMMANDS, F("  Battery:     %.2f V\n"), bat_voltage());

#ifdef BOARD_HAS_POWER_MGMT
    printfnl(SOURCE_COMMANDS, F("  Solar:       %.2f V\n"), solar_voltage());
#endif

    // ADC1 channels (GPIO 1-10 on ESP32-S3)
    printfnl(SOURCE_COMMANDS, F("\nADC1 (GPIO 1-10):\n"));
    for (int pin = 1; pin <= 10; pin++) {
        int mv = analogReadMilliVolts(pin);
        const char *name = pin_name_lookup(pin);
        if (name[0])
            printfnl(SOURCE_COMMANDS, F("  GPIO %2d: %4d mV  (%s)\n"), pin, mv, name);
        else
            printfnl(SOURCE_COMMANDS, F("  GPIO %2d: %4d mV\n"), pin, mv);
    }

    return 0;
}


int cmd_time(int argc, char **argv)
{
    static const char *dayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

    if (get_time_valid()) {
        int dow = get_day_of_week();
        if (dow < 0 || dow > 6) dow = 0;

        printfnl(SOURCE_COMMANDS, F("Time:   %04d-%02d-%02d %02d:%02d:%02d UTC (%s)\n"),
            get_year(), get_month(), get_day(),
            get_hour(), get_minute(), get_second(),
            dayNames[dow]);

        uint64_t epoch = get_epoch_ms();
        printfnl(SOURCE_COMMANDS, F("Epoch:  %lu%03lu ms\n"),
            (unsigned long)(epoch / 1000), (unsigned long)(epoch % 1000));
    } else {
        printfnl(SOURCE_COMMANDS, F("Time:   not available\n"));
    }

    // Show time source
    uint8_t ts = get_time_source();
    const char *src = "none";
    if (ts == 2)      src = "GPS+PPS";
    else if (ts == 1) src = "NTP";
    printfnl(SOURCE_COMMANDS, F("Source: %s\n"), src);
#ifdef BOARD_HAS_GPS
    printfnl(SOURCE_COMMANDS, F("GPS fix: %s  Sats: %d\n"), get_gpsstatus() ? "Yes" : "No", get_satellites());
#endif

    return 0;
}


int cmd_led(int argc, char **argv)
{
#ifdef BOARD_HAS_RGB_LEDS
    printfnl(SOURCE_COMMANDS, F("LED Config:\n"));
    printfnl(SOURCE_COMMANDS, F("  Strip 1: %d LEDs\n"), config.led_count1);
    printfnl(SOURCE_COMMANDS, F("  Strip 2: %d LEDs\n"), config.led_count2);
    printfnl(SOURCE_COMMANDS, F("  Strip 3: %d LEDs\n"), config.led_count3);
    printfnl(SOURCE_COMMANDS, F("  Strip 4: %d LEDs\n"), config.led_count4);
#else
    printfnl(SOURCE_COMMANDS, F("RGB LEDs not available on this board\n"));
#endif
    return 0;
}


int cmd_art( int argc, char **argv )
{
    getLock();
    Stream *out = getStream();
    out->print(
        "\n"
        "\033[38;5;208m"
        "            ▄\n"
        "           ███\n"
        "          █████\n"
        "\033[97m"
        "         ███████\n"
        "\033[38;5;208m"
        "        █████████\n"
        "       ███████████\n"
        "\033[97m"
        "      █████████████\n"
        "\033[38;5;208m"
        "     ███████████████\n"
        "    █████████████████\n"
        "   ███████████████████\n"
        "  █████████████████████\n"
        "\033[38;5;240m"
        " ▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄▄\n"
        "\033[0m"
        "\n"
        "      Is it art...?\n"
        "\n"
    );
    releaseLock();
    return 0;
}


// Helper: draw N copies of a UTF-8 character
static void wa_repeat(Stream *out, const char *ch, int n)
{
    for (int i = 0; i < n; i++) out->print(ch);
}

int cmd_winamp(int argc, char **argv)
{
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    // 40 spectrum bars, heights 0-8
    const int NBARS = 40, SROWS = 8;
    int bars[NBARS];
    for (int i = 0; i < NBARS; i++) bars[i] = esp_random() % (SROWS + 1);

    // Spectrum row colors: green -> yellow -> orange -> red (bottom to top)
    static const uint8_t spc[8][3] = {
        {0,170,0}, {0,210,0}, {0,255,0}, {100,255,0},
        {180,255,0}, {255,255,0}, {255,170,0}, {255,0,0}
    };

    // Inner width = 48.  All lines: indent + border + 48 content + border
    #define WA_W   48
    #define WA_IND "     "

    #define WF "\033[38;5;240m"
    #define WT "\033[38;5;208m"
    #define WG "\033[38;2;0;200;0m"
    #define WD "\033[38;5;242m"
    #define WB "\033[38;5;252m"
    #define WR "\033[0m"

    const int song_len = 213;  // 3:33
    int elapsed = 0;
    unsigned long last_sec = millis();

    getLock();
    Stream *out = getStream();
    out->print("\033[2J\033[?25l");
    releaseLock();

    for (;;) {
        // Advance clock
        if (millis() - last_sec >= 1000) {
            last_sec += 1000;
            elapsed++;
            if (elapsed >= song_len) elapsed = 0;
        }

        // Animate spectrum — drift with occasional spikes
        for (int i = 0; i < NBARS; i++) {
            bars[i] += (int)(esp_random() % 3) - 1;
            if (esp_random() % 8 == 0)
                bars[i] = 1 + (int)(esp_random() % 7);
            if (bars[i] < 0) bars[i] = 0;
            if (bars[i] > SROWS) bars[i] = SROWS;
        }

        int mm = elapsed / 60, ss = elapsed % 60;
        int seek = elapsed * 39 / (song_len > 0 ? song_len : 1); // 0-39

        getLock();
        out = getStream();
        out->print("\033[H\n\n\n");

        // --- Top border ---
        out->print(WF WA_IND "\xe2\x94\x8c");      // ┌
        wa_repeat(out, "\xe2\x94\x80", WA_W);       // ─ × 48
        out->print("\xe2\x94\x90\n");                // ┐

        // --- Title bar: 7 + 34 + 7 = 48 (□ is 2-wide) ---
        out->print(WF WA_IND "\xe2\x94\x82" WT      // │
            " WINAMP" WF
            "                                   "     // 34 spaces
            "- \xe2\x96\xa1 \xc3\x97 "               // - □ ×  (7 display chars)
            "\xe2\x94\x82\n");                       // │

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");       // ├
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");                // ┤

        // --- Time: "  ▶ XX:XX / 03:33" = 18 display (▶ is 2-wide), pad 30 ---
        out->printf(WF WA_IND "\xe2\x94\x82"
            "  " WG "\xe2\x96\xb6 %02d:%02d / 03:33" WF
            "                               "          // 30 spaces
            "\xe2\x94\x82\n", mm, ss);

        // --- Song: "  Rick Astley - Never Gonna Give You Up" = 39, pad 9 ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WG "Rick Astley - Never Gonna Give You Up" WF
            "         "                               // 9 spaces
            "\xe2\x94\x82\n");

        // --- Bitrate: "  128kbps  44kHz  stereo" = 24, pad 24 ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WD "128kbps  44kHz  stereo" WF
            "                        "               // 24 spaces
            "\xe2\x94\x82\n");

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");

        // --- 8-row spectrum (4 pad + 40 bars + 4 pad = 48) ---
        for (int row = SROWS - 1; row >= 0; row--) {
            out->printf(WF WA_IND "\xe2\x94\x82"
                "    \033[38;2;%d;%d;%dm",
                spc[row][0], spc[row][1], spc[row][2]);
            for (int i = 0; i < NBARS; i++)
                out->print(bars[i] > row ? "\xe2\x96\x88" : " ");
            out->print(WF "    \xe2\x94\x82\n");
        }

        // --- Separator ---
        out->print(WF WA_IND "\xe2\x94\x9c");
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\xa4\n");

        // --- Seek bar: "  " + 40 chars + "      " = 48 ---
        out->print(WF WA_IND "\xe2\x94\x82  " WD);
        for (int i = 0; i < 40; i++)
            out->print(i == seek
                ? (WG "\xe2\x97\x8f" WD)            // ● in green
                : "\xe2\x94\x80");                   // ─
        out->print(WF "      \xe2\x94\x82\n");   // 5 spaces (● is 2-wide)

        // --- Transport + volume slider ---
        out->print(WF WA_IND "\xe2\x94\x82"
            "  " WB
            "|\xe2\x97\x84  \xe2\x96\xb6  ||  "     // |◄  ▶  ||
            "\xe2\x96\xa0  \xe2\x96\xb6|" WF         //  ■  ▶|
            "   " WD "vol ");  // 3 spaces (◄/▶ are 2-wide)
        wa_repeat(out, "\xe2\x94\x80", 14);           // ─ × 14 (● is 2-wide)
        out->print(WG "\xe2\x97\x8f" WD);             // ● in green
        wa_repeat(out, "\xe2\x94\x80", 6);             // ─ × 4
        out->print(WF "  \xe2\x94\x82\n");

        // --- Bottom border ---
        out->print(WF WA_IND "\xe2\x94\x94");       // └
        wa_repeat(out, "\xe2\x94\x80", WA_W);
        out->print("\xe2\x94\x98" WR "\n");          // ┘

        out->print("\n" WA_IND "Any key to exit\n");

        releaseLock();

        vTaskDelay(pdMS_TO_TICKS(67));  // ~15 fps

        if (getStream()->available()) {
            while (getStream()->available()) getStream()->read();
            break;
        }
    }

    getLock();
    getStream()->print("\033[?25h" WR "\n");
    releaseLock();

    #undef WA_W
    #undef WA_IND
    #undef WF
    #undef WT
    #undef WG
    #undef WD
    #undef WB
    #undef WR

    return 0;
}


int cmd_game(int argc, char **argv)
{
    const int W = 30, H = 20;
    uint8_t grid[H][W], next[H][W], age[H][W];

    // Age palette: cyan-green → green → yellow-green → yellow → orange → red
    static const uint8_t pal[][3] = {
        {0,255,200}, {0,255,0}, {180,255,0},
        {255,220,0}, {255,128,0}, {255,0,0}
    };

    // Random initial state (~33% alive)
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            grid[y][x] = (esp_random() % 3 == 0);
            age[y][x] = grid[y][x];
        }

    // Drain leftover input from command entry (e.g. trailing \n after \r)
    vTaskDelay(pdMS_TO_TICKS(50));
    while (getStream()->available()) getStream()->read();

    getLock();
    Stream *out = getStream();
    out->print("\033[2J\033[?25l");   // clear screen + hide cursor
    releaseLock();

    for (int gen = 1; gen <= 500; gen++) {
        // Draw frame
        getLock();
        out = getStream();
        out->print("\033[H");         // cursor home

        for (int y = 0; y < H; y++) {
            int lc = -1;
            for (int x = 0; x < W; x++) {
                if (grid[y][x]) {
                    int a = age[y][x];
                    int c = (a<=1)?0 : (a<=3)?1 : (a<=6)?2 : (a<=10)?3 : (a<=16)?4 : 5;
                    if (c != lc) {
                        out->printf("\033[38;2;%d;%d;%dm", pal[c][0], pal[c][1], pal[c][2]);
                        lc = c;
                    }
                    out->print("\xe2\x96\x88\xe2\x96\x88");  // ██
                } else {
                    if (lc >= 0) { out->print("\033[0m"); lc = -1; }
                    out->print("  ");
                }
            }
            out->print("\033[0m\n");
        }
        out->printf("\033[0m Gen %-4d  Any key to exit", gen);
        releaseLock();

        vTaskDelay(pdMS_TO_TICKS(100));

        // Check for keypress to exit
        if (getStream()->available()) {
            while (getStream()->available()) getStream()->read();
            break;
        }

        // Compute next generation (toroidal wrap)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int n = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) {
                        if (!dy && !dx) continue;
                        n += grid[(y+dy+H)%H][(x+dx+W)%W];
                    }
                next[y][x] = grid[y][x] ? (n==2||n==3) : (n==3);
            }

        // Update grid and ages
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                age[y][x] = next[y][x]
                    ? (grid[y][x] ? (age[y][x] < 255 ? age[y][x]+1 : 255) : 1)
                    : 0;
                grid[y][x] = next[y][x];
            }
    }

    getLock();
    getStream()->print("\033[?25h\033[0m\n");  // show cursor + reset
    releaseLock();
    return 0;
}


int cmd_clear( int argc, char **argv )
{
    getLock();
    Stream *out = getStream();
    out->print(F("\033[2J\033[H"));  // clear screen + cursor home
    releaseLock();
    return 0;
}


int cmd_help( int argc, char **argv )
{
    printfnl( SOURCE_COMMANDS, F( "Available commands:\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  art                                 Is it art?\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  clear                               Clear screen\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  config [set|unset|reset]            Show or change settings\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  cue [load|start|stop|status]        Cue timeline engine\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  debug [off | {source} [on|off]]     Show or set debug message types\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  del {filename}                      Delete file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  game                                Waste time\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  dir                                 List files\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  gpio [set|out|in|read]              Show or configure GPIO pins\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  gps                                 Show GPS status\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  help                                Show this help\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  history                             Show command history\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  led                                 Show LED configuration\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  list {filename}                     Show file contents\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  load {filename}                     Load program (.bas or .wasm)\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  lora                                Show LoRa radio status\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  mem                                 Show heap memory stats\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  param {arg1} {arg2}                 Set program arguments\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ps                                  Show task list and stack watermarks\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  psram [test [forever]] [freq <MHz>] PSRAM status, test, or set clock\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  reboot                              Reboot the system\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ren {oldname} {newname}             Rename file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  run {filename}                      Run program (.bas or .wasm)\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  sensors                             Show sensor readings\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  stop                                Stop running program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  tc                                  Show thread count\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  time                                Show current date/time\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  uptime                              Show system uptime\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  version                             Show firmware version\n" ) );
#ifdef INCLUDE_WASM
    printfnl( SOURCE_COMMANDS, F( "  wasm [status|info <file>]           WASM runtime status/info\n" ) );
#endif
    printfnl( SOURCE_COMMANDS, F( "  wifi                                Show WiFi status\n" ) );
    return 0;
}


#ifdef INCLUDE_WASM
int cmd_wasm(int argc, char **argv)
{
    if (argc < 2 || !strcasecmp(argv[1], "status")) {
        printfnl(SOURCE_COMMANDS, F("WASM Runtime:\n"));
        printfnl(SOURCE_COMMANDS, F("  Running: %s\n"), wasm_is_running() ? "yes" : "no");
        if (wasm_is_running()) {
            const char *p = wasm_get_current_path();
            printfnl(SOURCE_COMMANDS, F("  Module:  %s\n"), (p && p[0]) ? p : "(unknown)");
        }
        return 0;
    }

    if (!strcasecmp(argv[1], "info")) {
        if (argc < 3) {
            printfnl(SOURCE_COMMANDS, F("Usage: wasm info <file.wasm>\n"));
            return 1;
        }
        File f = LittleFS.open(argv[2], "r");
        if (!f) {
            printfnl(SOURCE_COMMANDS, F("Cannot open %s\n"), argv[2]);
            return 1;
        }
        printfnl(SOURCE_COMMANDS, F("WASM Module: %s\n"), argv[2]);
        printfnl(SOURCE_COMMANDS, F("  Size: %u bytes\n"), (unsigned)f.size());
        f.close();
        return 0;
    }

    printfnl(SOURCE_COMMANDS, F("Usage: wasm [status | info <file>]\n"));
    return 1;
}
#endif

int cmd_psram(int argc, char **argv)
{
    if (argc >= 2 && !strcasecmp(argv[1], "test")) {
        bool forever = (argc >= 3 && !strcasecmp(argv[2], "forever"));
        return psram_test(forever);
    }
    if (argc >= 3 && !strcasecmp(argv[1], "freq")) {
        uint32_t mhz = strtol(argv[2], NULL, 10);
        if (mhz < 5 || mhz > 80) {
            printfnl(SOURCE_COMMANDS, F("Usage: psram freq <5-80>  (MHz)\n"));
            return 1;
        }
        if (psram_change_freq(mhz * 1000000) < 0) {
            printfnl(SOURCE_COMMANDS, F("Failed to change PSRAM frequency\n"));
            return 1;
        }
        uint32_t actual = psram_get_freq();
        if (actual != mhz * 1000000)
            printfnl(SOURCE_COMMANDS, F("PSRAM SPI clock: requested %u MHz, actual %.2f MHz\n"),
                     mhz, actual / 1000000.0f);
        else
            printfnl(SOURCE_COMMANDS, F("PSRAM SPI clock set to %u MHz\n"), mhz);
        return 0;
    }
    // Default: show status
    printfnl(SOURCE_COMMANDS, F("PSRAM:\n"));
    printfnl(SOURCE_COMMANDS, F("  Available:   %s\n"), psram_available() ? "yes" : "no");
    if (psram_get_freq())
        printfnl(SOURCE_COMMANDS, F("  SPI clock:   %.2f MHz\n"), psram_get_freq() / 1000000.0f);
    printfnl(SOURCE_COMMANDS, F("  Size:        %u bytes (%u KB)\n"), psram_size(), psram_size()/1024);
    printfnl(SOURCE_COMMANDS, F("  Used:        %u bytes\n"), psram_bytes_used());
    printfnl(SOURCE_COMMANDS, F("  Free:        %u bytes\n"), psram_bytes_free());
    printfnl(SOURCE_COMMANDS, F("  Contiguous:  %u bytes\n"), psram_bytes_contiguous());
    printfnl(SOURCE_COMMANDS, F("  Alloc slots: %d / %d\n"), psram_alloc_count(), psram_alloc_entries_max());
#if PSRAM_CACHE_PAGES > 0
    uint32_t hits = psram_cache_hits(), misses = psram_cache_misses();
    uint32_t total = hits + misses;
    printfnl(SOURCE_COMMANDS, F("  Cache:       %d x %d bytes (%u KB DRAM)\n"),
             PSRAM_CACHE_PAGES, PSRAM_CACHE_PAGE_SIZE,
             (PSRAM_CACHE_PAGES * PSRAM_CACHE_PAGE_SIZE) / 1024);
    printfnl(SOURCE_COMMANDS, F("  Cache hits:  %u / %u (%u%%)\n"),
             hits, total, total ? (hits * 100 / total) : 0);
#endif
    return 0;
}


void init_commands(Stream *dev)
{
    shell.attach(*dev);

    //Test Commands
    shell.addCommand(F("test"), test);

    //file Sydstem commands
    shell.addCommand(F("?"), cmd_help);
    shell.addCommand(F("art"), cmd_art);
    shell.addCommand(F("clear"), cmd_clear);
    shell.addCommand(F("cls"), cmd_clear);
    shell.addCommand(F("config"), cmd_config);
    shell.addCommand(F("cue"), cmd_cue);
    shell.addCommand(F("debug"), cmd_debug );
    shell.addCommand(F("del"), delFile);
    shell.addCommand(F("dir"), listDir);
    shell.addCommand(F("game"), cmd_game);
    shell.addCommand(F("gpio"), cmd_gpio);
    shell.addCommand(F("gps"), cmd_gps);
    shell.addCommand(F("help"), cmd_help);
    shell.addCommand(F("led"), cmd_led);
    shell.addCommand(F("list"), listFile);
    shell.addCommand(F("load"), loadFile);
    shell.addCommand(F("lora"), cmd_lora);
    shell.addCommand(F("mem"), cmd_mem);
    shell.addCommand(F("param"), paramBasic);
    shell.addCommand(F("ps"), cmd_ps);
    shell.addCommand(F("psram"), cmd_psram);
    shell.addCommand(F("reboot"), cmd_reboot );
    shell.addCommand(F("ren"), renFile);
    shell.addCommand(F("run"), runBasic);
    shell.addCommand(F("sensors"), cmd_sensors);
    shell.addCommand(F("stop"), stopBasic);
    shell.addCommand(F("tc"), tc);
    shell.addCommand(F("time"), cmd_time);
    shell.addCommand(F("uptime"), cmd_uptime);
    shell.addCommand(F("version"), cmd_version);
    shell.addCommand(F("wifi"), cmd_wifi);
    shell.addCommand(F("winamp"), cmd_winamp);
#ifdef INCLUDE_WASM
    shell.addCommand(F("wasm"), cmd_wasm);
#endif
}

void run_commands(void)
{
    shell.executeIfInput();
}

void setCLIEcho(bool echo)
{
  shell.setEcho(echo);
}