#ifndef basic_wrapper_h
#define basic_wrapper_h

#include <Arduino.h>


void setup_basic();
bool set_basic_program(Stream *output,char* prog);
void set_basic_param(uint8_t paramID, int val);  
int get_basic_param(int paramID);

#endif