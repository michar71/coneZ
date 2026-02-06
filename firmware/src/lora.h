#ifndef _conez_lora_h
#define _conez_lora_h

void lora_rx( void );
int lora_setup( void );

float lora_get_rssi(void);
float lora_get_snr(void);
float lora_get_frequency(void);
float lora_get_bandwidth(void);
int lora_get_sf(void);

#endif