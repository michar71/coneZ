#ifndef FWHOST_TASK_H
#define FWHOST_TASK_H
#include "freertos/FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

// "Sleeping" advances the deterministic clock instead of waiting.
void vTaskDelay(TickType_t ticks);

// The WASM task is never started: the runner calls wasm_run() directly.
static inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t,
                                                 void *, int, TaskHandle_t *, int) { return pdTRUE; }
#endif
