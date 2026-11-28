#ifndef FWHOST_SEMPHR_H
#define FWHOST_SEMPHR_H
#include "freertos/FreeRTOS.h"

typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
#endif
