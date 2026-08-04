/*
 * Configuration File Parser
 * Reads config.txt and extracts MQTT and device settings
 */

#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>

#define LAP_POLYGON_POINTS 4

typedef struct {
    double latitude;
    double longitude;
} GeoPoint;

typedef struct {
    GeoPoint points[LAP_POLYGON_POINTS];
    int point_count;
} LapPolygon;

typedef struct {
    char server[128];
    int port;
    char mqtt_user[64];
    char mqtt_password[64];
    char apn[64];
    char car_number[16];
    int device;
    LapPolygon start_polygon;
    LapPolygon finish_polygon;
    int min_lap_time_seconds;
} Config;

// Load configuration from file
bool config_load(const char *filename, Config *config);
bool config_polygon_contains(const LapPolygon *polygon, double latitude, double longitude);

// Print configuration (for debugging)
void config_print(const Config *config);

#endif // CONFIG_PARSER_H
