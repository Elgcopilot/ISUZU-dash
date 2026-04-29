/*
 * MQTT Client for Vehicle Telemetry
 * Sends vehicle data to MQTT broker using Paho MQTT C library
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include "config_parser.h"
#include "signals.h"

// Initialize MQTT client with configuration
bool mqtt_init(const Config *config);

// Publish vehicle telemetry data
bool mqtt_publish_telemetry(const VehicleData *data, const Config *config);

// Check if MQTT is connected
bool mqtt_is_connected(void);

// Cleanup MQTT client
void mqtt_cleanup(void);

#endif // MQTT_CLIENT_H
