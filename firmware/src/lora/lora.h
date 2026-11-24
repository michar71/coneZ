#ifndef _conez_lora_h
#define _conez_lora_h

#include <stddef.h>
#include <stdint.h>
#include "config.h"

// The per-channel PHY parameters -- everything a scanlist entry can vary. The
// rest of the radio setup (TX power, preamble, FSK shaping/CRC) always comes
// from config. Units match the config fields.
typedef struct {
    bool  fsk;                  // true = FSK, false = LoRa
    float freq_mhz;
    // LoRa
    float bw_khz;
    int   sf;
    int   cr;
    int   sync_word;            // same encoding as config.lora_sync_word
    // FSK
    float bitrate_kbps;
    float freqdev_khz;
    float rxbw_khz;
    bool  whitening;
    char  fsk_syncword[CONFIG_MAX_FSK_SYNCWORD];
} lora_phy_t;

void lora_rx( void );
int lora_setup( void );
void lora_start_task( void );                     // spawn the dedicated LoRa task (RX + scan)
void lora_set_active( bool on );                  // master on/off (`lora on|off`); sleeps/wakes radio
bool lora_is_active( void );                       // true while the LoRa subsystem is on
void lora_set_channel_rx_only( bool ro );         // scanner marks the tuned channel RX-only
bool lora_tx_allowed( void );                      // false on RX-only channel / [lora] rx_only / off

// lora_tx() return code when TX is refused (RX-only channel, [lora] rx_only, or LoRa off).
#define LORA_TX_INHIBITED  1
int lora_tx( const uint8_t *data, size_t len );   // transmit, then return to RX
void lora_print_beacon( void );                   // show last v1 BEACON in `lora` status
// Recursive mutex serializing all radio access + the scan state it drives. Held
// by the LoRa task around RX/scan; CLI radio/scan commands take it too.
void lora_radio_lock( void );
void lora_radio_unlock( void );
// Scanlist & channel lock API lives in scan.h.

float lora_get_rssi(void);   // RSSI of the last received packet (see lora_have_rx)
float lora_get_snr(void);    // SNR  of the last received packet (see lora_have_rx)
bool  lora_have_rx(void);    // true once a genuine packet has been received since boot
float lora_get_frequency(void);
float lora_get_bandwidth(void);
int lora_get_sf(void);
const char *lora_get_mode(void);
bool lora_is_fsk(void);
float lora_get_bitrate(void);
float lora_get_freqdev(void);
float lora_get_rxbw(void);
uint32_t lora_get_rx_count(void);
uint32_t lora_get_tx_count(void);
float lora_get_datarate(void);

int lora_set_frequency(float freq);
int lora_set_tx_power(int power);
int lora_set_bandwidth(float bw);
int lora_set_sf(int sf);
int lora_set_cr(int cr);
int lora_reinit(void);   // full re-init from config (for mode switch)
void lora_phy_from_config(lora_phy_t *p);   // snapshot the user's configured channel
int lora_reinit_phy(const lora_phy_t *p);   // full re-init on an explicit channel (scanner)
void lora_get_phy(lora_phy_t *out);         // the channel the radio is tuned to right now

#endif