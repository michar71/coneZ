#ifndef CONEZ_DIST_H
#define CONEZ_DIST_H

// ConeZ LoRa v1 dist (file distribution) -- cone receive side, Phase 3 core.
// Reassembles DIST_DATA chunks into the manifest + data files under /dist/,
// verifies MD5, and deletes local files no longer in the manifest. No
// compression/FEC yet (Phases 4-5); firmware OTA is Phase 6.

#include <stdint.h>
#include <stddef.h>

void dist_handle_chunk(const uint8_t *pkt, size_t len);  // from lora_rx() on DIST_DATA
void dist_print_status(void);                            // `dist` CLI command

#endif // CONEZ_DIST_H
