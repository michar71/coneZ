#ifndef SIM_WASM_IMPORTS_H
#define SIM_WASM_IMPORTS_H

#include "wasm3.h"

// Forward: each file provides a link function
M3Result link_led_imports(IM3Module module);
M3Result link_sensor_imports(IM3Module module);
M3Result link_datetime_imports(IM3Module module);
M3Result link_gpio_imports(IM3Module module);
M3Result link_system_imports(IM3Module module);
M3Result link_file_imports(IM3Module module);
M3Result link_io_imports(IM3Module module);
M3Result link_math_imports(IM3Module module);
M3Result link_format_imports(IM3Module module);
M3Result link_string_imports(IM3Module module);
M3Result link_compression_imports(IM3Module module);
M3Result link_deflate_imports(IM3Module module);

// Cleanup (called on program exit)
void wasm_close_all_files();
void wasm_reset_gamma();
void wasm_string_pool_reset();
void low_heap_init(uint32_t start);
void low_heap_reset(void);

// String pool helpers (used by file imports)
uint32_t pool_alloc(IM3Runtime runtime, int size);
int wasm_strlen(const uint8_t *mem, uint32_t mem_size, uint32_t ptr);

#endif
