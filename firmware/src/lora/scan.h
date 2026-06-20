#ifndef CONEZ_SCAN_H
#define CONEZ_SCAN_H

// LoRa v1 scanlist & channel lock (tiered, round-robin). See lora-protocol.txt
// section 15. The cone scans channels to find the master's beacon; channels are
// organised in priority TIERS (dist-delivered, root override, built-in default,
// then an exhaustive virtual sweep), scanned round-robin and widened over time.

void scan_init(void);                 // load scanlist + start scanning (from lora_setup)
void scan_notify_beacon(void);        // a beacon was decoded (from lora_handle_beacon)
void lora_scan_tick(void);            // drive the scan state machine (from the LoRa task)
void lora_scan_print(void);           // `lora` status
void lora_scan_set_enabled(bool en);  // `lora scan on|off`
bool lora_scan_is_enabled(void);      // true while scanning is enabled

#endif // CONEZ_SCAN_H
