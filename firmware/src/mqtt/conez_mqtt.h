#ifndef _conez_mqtt_client_h
#define _conez_mqtt_client_h

void mqtt_setup(void);
void mqtt_loop(void);

bool     mqtt_connected(void);
const char *mqtt_state_str(void);
uint32_t mqtt_uptime_sec(void);
uint32_t mqtt_tx_count(void);
uint32_t mqtt_rx_count(void);
// Publishes discarded because the queue was full (broker stalled or gone).
uint32_t mqtt_dropped_count(void);

void mqtt_force_connect(void);
void mqtt_force_disconnect(void);
int  mqtt_publish(const char *topic, const char *payload);

// Pop one command received on conez/{id}/cmd/# (topic suffix + payload), if any.
// Returns true and fills buf when a command was queued. Drained by ShellTask so the
// command runs in the interactive shell's context, not the MQTT event task.
bool mqtt_pop_command(char *buf, int bufsz);
// Commands discarded because the cmd queue was full.
uint32_t mqtt_cmd_dropped_count(void);

#endif
