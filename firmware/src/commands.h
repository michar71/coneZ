#ifndef commands_h
#define commands_h

#include <Arduino.h>

#define FSLINK LittleFS

void init_commands(Stream *dev);
void run_commands(void);

#endif