#ifndef wasm_wrapper_h
#define wasm_wrapper_h

#include <stdint.h>
#include <stdbool.h>

void setup_wasm();
bool set_wasm_program(const char *path);
bool wasm_is_running(void);
void wasm_request_stop(void);
const char *wasm_get_current_path(void);

#endif
