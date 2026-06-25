#ifndef CONEZ_SCAN_H
#define CONEZ_SCAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// LoRa v1 scanlist & channel lock (tiered, round-robin). See lora-protocol.txt
// section 15. The cone scans channels to find the master's beacon; channels are
// organised in priority TIERS (dist-delivered, root override, built-in default,
// then an exhaustive virtual sweep), scanned round-robin and widened over time.

// One scanlist channel / tuned PHY. Exposed so lora.cpp can turn a decoded beacon
// into the channel it ADVERTISES and hand it to scan_notify_beacon() for the
// follow check (see scan.cpp).
typedef struct {
    uint8_t  mode;                 // LP_MODE_LORA / LP_MODE_FSK
    bool     rx_only;              // LX/FX: listen only, never TX here
    bool     whitening;            // FW: FSK data whitening (seed 0x01FF); ignored for LoRa
    uint32_t freq_hz;
    uint32_t bw_hz;                // LoRa bandwidth
    uint8_t  sf, cr;               // LoRa
    uint16_t sync_word;            // LoRa 16-bit sync
    uint32_t bitrate_bps, freqdev_hz, rxbw_hz;  // FSK
    char     fsk_sync[17];         // FSK sync word as hex string
} scan_entry_t;

void scan_init(void);                 // load scanlist + start scanning (from lora_setup)
// A beacon was decoded; `advertised` is the channel it advertises (may be NULL).
// If that PHY differs from the one we're tuned to, the master is announcing a move,
// so we retune to it and stay locked instead of waiting to lose lock and rescan.
void scan_notify_beacon(const scan_entry_t *advertised);
void lora_scan_tick(void);            // drive the scan state machine (from the LoRa task)
void lora_scan_print(void);           // `lora` status
void lora_scan_set_enabled(bool en);  // `lora scan on|off`
bool lora_scan_is_enabled(void);      // true while scanning is enabled

#endif // CONEZ_SCAN_H
