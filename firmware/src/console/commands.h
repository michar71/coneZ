#ifndef commands_h
#define commands_h

#include "conez_stream.h"

void init_commands(ConezStream *dev);
void run_commands(void);
void setCLIEcho(bool echo);

#endif
