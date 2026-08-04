#ifndef TRACK_CONFIG_H
#define TRACK_CONFIG_H

#include <stdbool.h>
#include "config_parser.h"

#define TRACK_ID_MAX_LENGTH 64
#define TRACK_NAME_MAX_LENGTH 128

typedef enum {
    TRACK_LAP_MODE_SEPARATE,
    TRACK_LAP_MODE_SHARED
} TrackLapMode;

typedef struct {
    char track_id[TRACK_ID_MAX_LENGTH];
    char track_name[TRACK_NAME_MAX_LENGTH];
    char event_id[TRACK_ID_MAX_LENGTH];
    char event_name[TRACK_NAME_MAX_LENGTH];
    TrackLapMode lap_mode;
    int min_lap_time_seconds;
    float min_speed_kmh;
    LapPolygon start_polygon;
    LapPolygon finish_polygon;
} TrackConfig;

bool track_config_load(const char *filename, TrackConfig *track);

#endif // TRACK_CONFIG_H