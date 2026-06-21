#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota_lock.h"

// See ota_lock.h. A plain flag under a portMUX spinlock: any task may release, so
// a LoRa-task-held OTA can be torn down from the shell on `lora off`. The critical
// section only brackets the test-and-set, never a flash op.
static portMUX_TYPE s_mux   = portMUX_INITIALIZER_UNLOCKED;
static bool         s_busy  = false;
static const char  *s_owner = NULL;

bool ota_lock_acquire(const char *who)
{
    bool ok;
    portENTER_CRITICAL(&s_mux);
    ok = !s_busy;
    if (ok) { s_busy = true; s_owner = who; }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

void ota_lock_release(void)
{
    portENTER_CRITICAL(&s_mux);
    s_busy = false; s_owner = NULL;
    portEXIT_CRITICAL(&s_mux);
}

bool        ota_lock_held(void)  { return s_busy; }
const char *ota_lock_owner(void) { return s_owner; }
