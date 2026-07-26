#ifndef basic_wrapper_h
#define basic_wrapper_h

#include <stdint.h>
#include <stdbool.h>

// Params are always available (shared between BASIC and WASM runtimes)
#define BASIC_MAX_PARAMS 16

// Valid ids are 0..BASIC_MAX_PARAMS-1. Out-of-range ids are REJECTED, not
// clamped: set returns false and writes nothing, get returns 0. (paramID is
// int, not uint8_t -- a narrow type silently truncated id 256 to 0, which is
// the stop flag.)
bool set_basic_param(int paramID, int val);
int get_basic_param(int paramID);
bool basic_param_valid(int paramID);

// Script routing: auto-detects .bas vs .wasm by extension
bool set_script_program(char *path);

#ifdef INCLUDE_BASIC
void setup_basic();
bool set_basic_program(char *prog);
#endif

#endif
