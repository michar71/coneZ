//Printmanager manages all printout to serial or telnet consoles and is thread-safe.
//ALL text output outside of Setup() needs to go through the printManager functions !!!
//This is to ensure that all text output is thread-safe and does not collide with other threads.

//All printing should happen through printfl which makes sure all lines are terminated with /n
#include <Arduino.h>

#ifndef PRINTMANAGER_H
#define PRINTMANAGER_H


typedef enum {
    SOURCE_BASIC        = 0x00000001,
    SOURCE_WASM         = 0x00000002,
                                        // Available: 0x00000004
                                        // Available: 0x00000008
                                        // Available: 0x00000010
                                        // Available: 0x00000020
                                        // Available: 0x00000040
                                        // Available: 0x00000080
    SOURCE_SHELL        = 0x00000100,
    SOURCE_COMMANDS     = 0x00000200,
    SOURCE_SYSTEM       = 0x00000400,
                                        // Available: 0x00000800
    SOURCE_GPS          = 0x00001000,
    SOURCE_GPS_RAW      = 0x00002000,
                                        // Available: 0x00004000
                                        // Available: 0x00008000
    SOURCE_LORA         = 0x00010000,
    SOURCE_LORA_RAW     = 0x00020000,
                                        // Available: 0x00040000
                                        // Available: 0x00080000
    SOURCE_FSYNC        = 0x00100000,
                                        // Available: 0x00200000
                                        // Available: 0x00400000
                                        // Available: 0x00800000
    SOURCE_SENSORS      = 0x01000000,
                                        // Available: 0x02000000
                                        // Available: 0x04000000
                                        // Available: 0x08000000
    SOURCE_WIFI         = 0x10000000,
                                        // Available: 0x20000000
    SOURCE_OTHER        = 0x40000000,
    SOURCE_NONE         = 0x80000000
} source_e;


// Print function with source prefix and carriage return
//void printfl(source_e source, const char *format, ...);

// Print wirthout carriage return
void printfnl(source_e source, const char *format, ...);
void printfnl( source_e source, const __FlashStringHelper *format, ... );

// Set the output stream (e.g., Serial or Telnet)
void setStream(Stream *stream);

// Set debug level for a specific system
void setDebugLevel(source_e system, bool enable);

// Turn off all debug messages
void setDebugOff( void );

// Show or hide timestamps in the output
void showTimestamps(bool enable);

//Initalize printManager
void printManagerInit(Stream* defaultStream);

Stream* getStream(void);

void getLock(void);
void releaseLock(void);

bool getDebug(source_e source);
void inc_thread_count(int thread);
long get_thread_count(int thread);

#endif