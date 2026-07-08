#ifndef MQTT_NODE_STATUS_H
#define MQTT_NODE_STATUS_H

#include <stddef.h>

#include "syst_status.h"

#define MQTT_NODE_STATUS_JSON_MAX 512

int mqtt_node_status_format_json(const node_status *status, char *buf, size_t bufsz);

#endif
