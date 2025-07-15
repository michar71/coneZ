//Printmanager manages all printout to Srrial or telnet consoles and is thread-safe.
//ALL text output outside of Setup() needs to go through the printManager functions !!!
//This is to ensure that all text output is thread-safe and does not collide with other threads.

//All printing should happen through printfl which makes sure all lines are terminated with /n
#include <Arduino.h>

#ifndef PRINTMANAGER_H
#define PRINTMANAGER_H


typedef enum {
    SOURCE_BASIC = 1,
    SOURCE_SHELL = 2,
    SOURCE_COMMANDS = 4,
    SOURCE_SYSTEM = 8,
    SOURCE_GPS = 16,
    SOURCE_LORA = 32,
    SOURCE_OTHER = 64,
    SOURCE_NONE = 128
} source_e;


// Print function with source prefix and carriage return
//void printfl(source_e source, const char *format, ...);

//PRint wirthout carriage return
void printfnl(source_e source, const char *format, ...);

// Set the output stream (e.g., Serial or Telnet)
void setStream(Stream *stream);

// Set debug level for a specific system
void setDebugLevel(source_e system, bool enable);

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