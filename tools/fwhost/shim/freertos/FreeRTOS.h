// Host shim for FreeRTOS -- just enough for the firmware's WASM host files.
#ifndef FWHOST_FREERTOS_H
#define FWHOST_FREERTOS_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t TickType_t;
typedef int BaseType_t;
#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY 0xffffffffu
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define tskNO_AFFINITY 0x7fffffff

static inline int xPortGetCoreID(void) { return 0; }

#endif
