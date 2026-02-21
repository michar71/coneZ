#ifndef basic_wrapper_h
#define basic_wrapper_h

#include <stdint.h>
#include <stdbool.h>

// Params are always available (shared between BASIC and WASM runtimes)
void set_basic_param(uint8_t paramID, int val);
int get_basic_param(int paramID);

// Script routing: auto-detects .bas vs .wasm by extension
bool set_script_program(char *path);

#ifdef INCLUDE_BASIC
void setup_basic();
bool set_basic_program(char *prog);
#endif

#endif
