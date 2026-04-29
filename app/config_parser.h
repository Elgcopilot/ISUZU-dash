/*
 * Configuration File Parser
 * Reads config.txt and extracts MQTT and device settings
 */

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>

typedef struct {
    char server[128];
    int port;
    char mqtt_user[64];
    char mqtt_password[64];
    char apn[64];
    char car_number[16];
    int device;
} Config;

// Load configuration from file
bool config_load(const char *filename, Config *config);

// Print configuration (for debugging)
void config_print(const Config *config);

#endif // CONFIG_PARSER_H
