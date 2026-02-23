#ifndef wasm_internal_h
#define wasm_internal_h

#include "wasm3.h"
#include "m3_config.h"

// Shared state (defined in wasm_wrapper.cpp)
extern volatile bool wasm_stop_requested;

// Link functions (each defined in its own wasm_imports_*.cpp / wasm_format.cpp)
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

// Cleanup functions (called from wasm_run() on program exit)
void wasm_close_all_files(void);   // defined in wasm_imports_file.cpp
void wasm_reset_gamma(void);       // defined in wasm_imports_led.cpp
void wasm_string_pool_reset(void); // defined in wasm_imports_string.cpp

// String pool helpers (defined in wasm_imports_string.cpp, used by file imports)
uint32_t pool_alloc(IM3Runtime runtime, int size);
int wasm_strlen(const uint8_t *mem, uint32_t mem_size, uint32_t ptr);

// WASM linear memory helpers (defined in wasm_psram_glue.cpp)
// Routes through PSRAM when d_m3UsePsramMemory=1, direct DRAM otherwise.
void     wasm_mem_read(IM3Runtime rt, uint32_t offset, void *dst, size_t len);
void     wasm_mem_write(IM3Runtime rt, uint32_t offset, const void *src, size_t len);
uint32_t wasm_mem_size(IM3Runtime rt);
bool     wasm_mem_check(IM3Runtime rt, uint32_t offset, size_t len);
int      wasm_mem_read_str(IM3Runtime rt, uint32_t offset, char *dst, size_t max);
int      wasm_mem_strlen(IM3Runtime rt, uint32_t ptr);
uint8_t  wasm_mem_read8(IM3Runtime rt, uint32_t offset);
void     wasm_mem_write8(IM3Runtime rt, uint32_t offset, uint8_t val);
void     wasm_mem_copy(IM3Runtime rt, uint32_t dst, uint32_t src, size_t len);
void     wasm_mem_set(IM3Runtime rt, uint32_t offset, uint8_t val, size_t len);

#endif
