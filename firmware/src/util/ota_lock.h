#ifndef CONEZ_OTA_LOCK_H
#define CONEZ_OTA_LOCK_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mutual exclusion for the "big flash + reboot" operations that must never run
// concurrently because they target overlapping flash (the inactive OTA app slot
// or the filesystem partition) and reboot: HTTP firmware OTA, HTTP filesystem
// upload, and the LoRa dist firmware OTA. Without this they can interleave
// erase/write on the same partition and produce a corrupt -- in the worst race,
// unbootable -- image.
//
// Non-owning test-and-set (portMUX-guarded flag, NOT a FreeRTOS mutex): a transfer
// acquired on one task (e.g. the LoRa task) can be released from another (e.g. the
// shell task aborting it on `lora off`). acquire() never blocks.
bool        ota_lock_acquire(const char *who);  // true if acquired; false if already held
void        ota_lock_release(void);
bool        ota_lock_held(void);
const char *ota_lock_owner(void);               // holder name, or NULL when free

#ifdef __cplusplus
}
#endif

#endif // CONEZ_OTA_LOCK_H
