#ifndef _conez_mqtt_client_h
#define _conez_mqtt_client_h

void mqtt_setup(void);
void mqtt_loop(void);

bool     mqtt_connected(void);
const char *mqtt_state_str(void);
uint32_t mqtt_uptime_sec(void);
uint32_t mqtt_tx_count(void);
uint32_t mqtt_rx_count(void);

void mqtt_force_connect(void);
void mqtt_force_disconnect(void);
int  mqtt_publish(const char *topic, const char *payload);

#endif
