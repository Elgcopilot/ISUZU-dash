/*
 * Configuration File Parser - Implementation
 */

#include "config_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Parse a line in format "#KEY:VALUE"
static bool parse_line(const char *line, const char *key, char *value, size_t value_size) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Check if line starts with #
    if (*line != '#') return false;
    line++;
    
    // Check if key matches
    size_t key_len = strlen(key);
    if (strncmp(line, key, key_len) != 0) return false;
    line += key_len;
    
    // Check for colon
    if (*line != ':') return false;
    line++;
    
    // Copy value (trim trailing newline/whitespace)
    strncpy(value, line, value_size - 1);
    value[value_size - 1] = '\0';
    
    // Remove trailing newline
    char *newline = strchr(value, '\n');
    if (newline) *newline = '\0';
    newline = strchr(value, '\r');
    if (newline) *newline = '\0';
    
    return true;
}

bool config_load(const char *filename, Config *config) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror("Failed to open config file");
        return false;
    }
    
    char line[256];
    char value[128];
    
    // Set defaults
    memset(config, 0, sizeof(Config));
    config->port = 1883;
    config->device = 1;
    
    while (fgets(line, sizeof(line), fp)) {
        if (parse_line(line, "SERVER", value, sizeof(value))) {
            strncpy(config->server, value, sizeof(config->server) - 1);
        }
        else if (parse_line(line, "PORT", value, sizeof(value))) {
            config->port = atoi(value);
        }
        else if (parse_line(line, "MQTT_USER", value, sizeof(value))) {
            strncpy(config->mqtt_user, value, sizeof(config->mqtt_user) - 1);
        }
        else if (parse_line(line, "MQTT_PASSWORD", value, sizeof(value))) {
            strncpy(config->mqtt_password, value, sizeof(config->mqtt_password) - 1);
        }
        else if (parse_line(line, "APN", value, sizeof(value))) {
            strncpy(config->apn, value, sizeof(config->apn) - 1);
        }
        else if (parse_line(line, "CAR-NUMBER", value, sizeof(value))) {
            strncpy(config->car_number, value, sizeof(config->car_number) - 1);
        }
        else if (parse_line(line, "DEVICE", value, sizeof(value))) {
            config->device = atoi(value);
        }
    }
    
    fclose(fp);
    
    // Validate required fields
    if (strlen(config->server) == 0) {
        fprintf(stderr, "Config error: SERVER not specified\n");
        return false;
    }
    
    printf("Config loaded: %s:%d (device=%d, car=%s)\n", 
           config->server, config->port, config->device, config->car_number);
    
    return true;
}

void config_print(const Config *config) {
    printf("=== Configuration ===\n");
    printf("Server: %s:%d\n", config->server, config->port);
    printf("User: %s\n", config->mqtt_user);
    printf("APN: %s\n", config->apn);
    printf("Car Number: %s\n", config->car_number);
    printf("Device: %d\n", config->device);
    printf("====================\n");
}
