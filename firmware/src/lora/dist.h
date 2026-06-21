#ifndef CONEZ_DIST_H
#define CONEZ_DIST_H

// ConeZ LoRa v1 dist (file distribution) -- cone receive side.
// Reassembles DIST_DATA chunks (per block) into the manifest + data files under
// /dist/, inflating per-block deflate (Phase 4), verifies MD5, and deletes local
// files no longer in the manifest. No FEC yet (Phase 5); firmware OTA is Phase 6.

#include <stdint.h>
#include <stddef.h>

void dist_handle_chunk(const uint8_t *pkt, size_t len);  // from lora_rx() on DIST_DATA
void dist_abort(void);                                   // free in-progress transfer (e.g. `lora off`)
void dist_tick(void);                                    // periodic stall check (from the LoRa task)
void dist_print_status(void);                            // `dist` CLI command
void dist_ota_selftest(int kb, int yield_ms);            // `dist test <kb> [yield_ms]`

#endif // CONEZ_DIST_H
