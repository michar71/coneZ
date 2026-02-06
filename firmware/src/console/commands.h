#ifndef commands_h
#define commands_h

#include <Arduino.h>

void init_commands(Stream *dev);
void run_commands(void);
void setCLIEcho(bool echo);

#endif