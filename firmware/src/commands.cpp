#include "commands.h"
#include <LittleFS.h>
#include <FS.h>
#include <SimpleSerialShell.h>
#include "basic_wrapper.h"
#include "main.h"
#include "task.h"
#include "printManager.h"


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
  
    while (file.available()) 
    {
      printfnl(SOURCE_COMMANDS, F("%c\n"), file.read());
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
    deleteFile(FSLINK,argv[1]);
    return 0;
}

int renFile(int argc, char **argv) 
{
    if (argc != 3)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }
    renameFile(FSLINK,argv[1], argv[2]);
    return 0;
}

int listFile(int argc, char **argv) 
{
    if (argc != 2)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;
    }

    readFile(FSLINK,argv[1]); 
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
    listDir(FSLINK,"/",1); 
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
        File file = FSLINK.open(argv[1], FILE_WRITE);
        if (!file) 
        {
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
                        if (file.print(line)) 
                        {
                        } 
                        else 
                        {
                          getStream()->printf("Write Error\n");
                          file.close();
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
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;       
    }
    else
    {
        if (false == set_basic_program(argv[1]))
          printfnl(SOURCE_COMMANDS, F("BASIC code already running\n") );
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

/*
 void vTaskGetRunTimeStats( char *pcWriteBuffer )
{
    TaskStatus_t pxTaskStatusArray[20];
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime, ulStatsAsPercentage;

        // Make sure the write buffer does not contain a string.
    *pcWriteBuffer = 0x00;

    // Take a snapshot of the number of tasks in case it changes while this
    // function is executing.
    uxArraySize = uxTaskGetNumberOfTasks();

    // Allocate a TaskStatus_t structure for each task.  An array could be
    // allocated statically at compile time.
    //pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );

    if( pxTaskStatusArray != NULL )
    {
        // Generate raw status information about each task.
        uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulTotalRunTime );

        // For percentage calculations.
        ulTotalRunTime /= 100UL;

        // Avoid divide by zero errors.
        if( ulTotalRunTime > 0 )
        {
            // For each populated position in the pxTaskStatusArray array,
            // format the raw data as human readable ASCII data
            for( x = 0; x < uxArraySize; x++ )
            {
                // What percentage of the total run time has the task used?
                // This will always be rounded down to the nearest integer.
                // ulTotalRunTimeDiv100 has already been divided by 100.
                ulStatsAsPercentage = pxTaskStatusArray[ x ].ulRunTimeCounter / ulTotalRunTime;

                if( ulStatsAsPercentage > 0UL )
                {
                    sprintf( pcWriteBuffer, "%s\t\t%lu\t\t%lu%%\r\n", pxTaskStatusArray[ x ].pcTaskName, pxTaskStatusArray[ x ].ulRunTimeCounter, ulStatsAsPercentage );
                }
                else
                {
                    // If the percentage is zero here then the task has
                    // consumed less than 1% of the total run time.
                    sprintf( pcWriteBuffer, "%s\t\t%lu\t\t<1%%\r\n", pxTaskStatusArray[ x ].pcTaskName, pxTaskStatusArray[ x ].ulRunTimeCounter );
                }

                pcWriteBuffer += strlen( ( char * ) pcWriteBuffer );
            }
        }

        // The array is no longer needed, free the memory it consumes.
        vPortFree( pxTaskStatusArray );
    }
}
 */
int tc(int argc, char **argv) 
{
    char buf[1024];
    if (argc != 1)
    {
        printfnl(SOURCE_COMMANDS, F("Wrong argument count\n") );
        return 1;       
    }
    else
    {
      /*
        vTaskGetRunTimeStats(buf);
        printfl(SOURCE_COMMANDS,"Task List:");
        printfl(SOURCE_COMMANDS,"%s",buf);
        printfl(SOURCE_COMMANDS,"");
        */
        printfnl(SOURCE_COMMANDS,F("Thread Count:\n") );
        for (int ii=0;ii<4;ii++)
        {
            printfnl( SOURCE_COMMANDS, F("Core %d: %d\n"), (uint8_t)ii, (unsigned int)get_thread_count(ii) );
        }
        return 0;
    }
}


int cmd_help( int argc, char **argv )
{
    printfnl( SOURCE_COMMANDS, F( "Available commands:\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ?                                  Show help\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  debug [off | {source} [on|off]]    Show or set debug message types\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  del {filename}                     Delete file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  dir                                List files\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  help                               Crash the main thread\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  list {filename}                    Show file contents\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  load {filename}                    Load BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  param {arg1} {arg2}                Set BASIC program arguments\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  reboot                             Respawn as a coyote\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  ren {oldname} {newname}            Rename file\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  run {filename}                     Run BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  stop                               Stop BASIC program\n" ) );
    printfnl( SOURCE_COMMANDS, F( "  tc                                 Show thread count\n\n" ) );
    return 0;
}


void init_commands(Stream *dev)
{
    shell.attach(*dev);

    //Test Commands
    shell.addCommand(F("test"), test);

    //file Sydstem commands
    shell.addCommand(F("?"), cmd_help);
    shell.addCommand(F("debug"), cmd_debug );
    shell.addCommand(F("del"), delFile);
    shell.addCommand(F("dir"), listDir);
    shell.addCommand(F("help"), cmd_help);
    shell.addCommand(F("list"), listFile);
    shell.addCommand(F("load"), loadFile);
    shell.addCommand(F("param"), paramBasic);
    shell.addCommand(F("reboot"), cmd_reboot );
    shell.addCommand(F("ren"), renFile);
    shell.addCommand(F("run"), runBasic);
    shell.addCommand(F("stop"), stopBasic);
    shell.addCommand(F("tc"), tc);
    
    
    //System commands

    //Basic Commands
}

void run_commands(void)
{
    shell.executeIfInput();
}

void setCLIEcho(bool echo)
{
  shell.setEcho(echo);
}