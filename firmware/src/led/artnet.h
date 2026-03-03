#ifndef ARTNET_H
#define ARTNET_H

#include <stdint.h>
#include <stdbool.h>

// Call in setup() after config_init() and led_setup(). Starts the receiver
// task immediately if config.artnet_enabled is true.
void artnet_setup(void);

// Start/stop the receiver task. artnet_start() is idempotent (no-op if
// already running). artnet_stop() signals the task to exit; it finishes
// within ~200 ms (recvfrom timeout). Neither call saves config.
void artnet_start(void);
void artnet_stop(void);

bool     artnet_running(void);
uint32_t artnet_rx_packets(void);   // valid OpOutput packets received
uint32_t artnet_rx_frames(void);    // LED buffer updates applied

#endif
