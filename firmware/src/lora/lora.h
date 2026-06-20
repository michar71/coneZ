#ifndef _conez_lora_h
#define _conez_lora_h

#include <stddef.h>
#include <stdint.h>

void lora_rx( void );
int lora_setup( void );
void lora_start_task( void );                     // spawn the dedicated LoRa task (RX + scan)
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

#endif