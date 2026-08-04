/*
 * Configuration File Parser - Implementation
 */

#include "config_parser.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool parse_polygon(const char *value, LapPolygon *polygon) {
    const char *cursor = value;

    polygon->point_count = 0;
    for (int i = 0; i < LAP_POLYGON_POINTS; i++) {
        char *end;
        double latitude = strtod(cursor, &end);
        if (end == cursor || *end != ',') return false;

        cursor = end + 1;
        double longitude = strtod(cursor, &end);
        if (end == cursor) return false;

        polygon->points[i].latitude = latitude;
        polygon->points[i].longitude = longitude;
        polygon->point_count++;
        cursor = end;

        if (i < LAP_POLYGON_POINTS - 1) {
            if (*cursor != ';') return false;
            cursor++;
        }
    }

    return *cursor == '\0';
}

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
    
    char line[512];
    char value[512];
    
    // Set defaults
    memset(config, 0, sizeof(Config));
    config->port = 1883;
    config->device = 1;
    config->min_lap_time_seconds = 10;
    
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
        else if (parse_line(line, "START", value, sizeof(value))) {
            if (!parse_polygon(value, &config->start_polygon)) {
                fprintf(stderr, "Config warning: invalid START polygon\n");
            }
        }
        else if (parse_line(line, "FINISH", value, sizeof(value))) {
            if (!parse_polygon(value, &config->finish_polygon)) {
                fprintf(stderr, "Config warning: invalid FINISH polygon\n");
            }
        }
        else if (parse_line(line, "LAP_MIN_SECONDS", value, sizeof(value))) {
            int seconds = atoi(value);
            if (seconds > 0) config->min_lap_time_seconds = seconds;
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

bool config_polygon_contains(const LapPolygon *polygon, double latitude, double longitude) {
    if (polygon == NULL || polygon->point_count < 3) return false;

    bool inside = false;
    for (int i = 0, j = polygon->point_count - 1; i < polygon->point_count; j = i++) {
        const GeoPoint *a = &polygon->points[i];
        const GeoPoint *b = &polygon->points[j];
        bool crosses_latitude = (a->latitude > latitude) != (b->latitude > latitude);
        double crossing_longitude = (b->longitude - a->longitude) *
                                   (latitude - a->latitude) /
                                   (b->latitude - a->latitude) + a->longitude;
        if (crosses_latitude && longitude < crossing_longitude) inside = !inside;
    }
    return inside;
}

void config_print(const Config *config) {
    printf("=== Configuration ===\n");
    printf("Server: %s:%d\n", config->server, config->port);
    printf("User: %s\n", config->mqtt_user);
    printf("APN: %s\n", config->apn);
    printf("Car Number: %s\n", config->car_number);
    printf("Device: %d\n", config->device);
    printf("Lap zones: start=%d points, finish=%d points, min=%ds\n",
           config->start_polygon.point_count, config->finish_polygon.point_count,
           config->min_lap_time_seconds);
    printf("====================\n");
}
