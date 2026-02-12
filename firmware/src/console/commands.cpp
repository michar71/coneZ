#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include <SimpleSerialShell.h>
#include <WiFi.h>
#include "esp_system.h"
#include "esp_heap_caps.h"
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


int cmd_gps(int argc, char **argv)
{
#ifdef BOARD_HAS_GPS
    printfnl(SOURCE_COMMANDS, F("GPS Status:\n"));
    printfnl(SOURCE_COMMANDS, F("  Fix:        %s\n"), get_gpsstatus() ? "Yes" : "No");
    printfnl(SOURCE_COMMANDS, F("  Satellites: %d\n"), get_satellites());
    printfnl(SOURCE_COMMANDS, F("  HDOP:       %.2f\n"), get_hdop() / 100.0);
    printfnl(SOURCE_COMMANDS, F("  Position:   %.6f, %.6f\n"), get_lat(), get_lon());
    printfnl(SOURCE_COMMANDS, F("  Altitude:   %.0f m\n"), get_alt());
    printfnl(SOURCE_COMMANDS, F("  Speed:      %.1f m/s\n"), get_speed());
    printfnl(SOURCE_COMMANDS, F("  Direction:  %.1f deg\n"), get_dir());
#else
    printfnl(SOURCE_COMMANDS, F("GPS not available on this board\n"));
#endif
    return 0;
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


int cmd_help( int argc, char **argv )
{
    printfnl( SOURCE_COMMANDS, F( "Available commands:\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ?                                  Show help\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  config [set|unset|reset]           Show or change settings\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  cue [load|start|stop|status]       Cue timeline engine\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  debug [off | {source} [on|off]]    Show or set debug message types\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  del {filename}                     Delete file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  dir                                List files\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  gps                                Show GPS status\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  help                               Crash the main thread\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  led                                Show LED configuration\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  list {filename}                    Show file contents\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  load {filename}                    Load BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  lora                               Show LoRa radio status\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  mem                                Show heap memory stats\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  param {arg1} {arg2}                Set BASIC program arguments\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ps                                 Show task list and stack watermarks\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  reboot                             Respawn as a coyote\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ren {oldname} {newname}            Rename file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  run {filename}                     Run BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  sensors                            Show sensor readings\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  stop                               Stop BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  tc                                 Show thread count\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  time                               Show current date/time\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  uptime                             Show system uptime\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  psram [test [forever]] [freq <MHz>] PSRAM status, test, or set SPI clock\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  version                            Show firmware version\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  wasm [status|info <file>]           WASM runtime status/info\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  wifi                               Show WiFi status\n\n" ) );
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
    shell.addCommand(F("config"), cmd_config);
    shell.addCommand(F("cue"), cmd_cue);
    shell.addCommand(F("debug"), cmd_debug );
    shell.addCommand(F("del"), delFile);
    shell.addCommand(F("dir"), listDir);
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