#include "track_config.h"

#include <json-c/json.h>
#include <stdio.h>
#include <string.h>

static bool copy_required_string(struct json_object *object, const char *key,
                                 char *destination, size_t destination_size) {
    struct json_object *value;
    if (!json_object_object_get_ex(object, key, &value) ||
        !json_object_is_type(value, json_type_string)) {
        return false;
    }

    snprintf(destination, destination_size, "%s", json_object_get_string(value));
    return destination[0] != '\0';
}

static bool parse_polygon(struct json_object *object, const char *key, LapPolygon *polygon) {
    struct json_object *points;
    if (!json_object_object_get_ex(object, key, &points) ||
        !json_object_is_type(points, json_type_array) ||
        json_object_array_length(points) != LAP_POLYGON_POINTS) {
        return false;
    }

    polygon->point_count = 0;
    for (int i = 0; i < LAP_POLYGON_POINTS; i++) {
        struct json_object *point = json_object_array_get_idx(points, i);
        struct json_object *latitude;
        struct json_object *longitude;
        if (!json_object_is_type(point, json_type_object) ||
            !json_object_object_get_ex(point, "lat", &latitude) ||
            !json_object_object_get_ex(point, "lon", &longitude) ||
            !json_object_is_type(latitude, json_type_double) ||
            !json_object_is_type(longitude, json_type_double)) {
            return false;
        }

        polygon->points[i].latitude = json_object_get_double(latitude);
        polygon->points[i].longitude = json_object_get_double(longitude);
        polygon->point_count++;
    }

    return true;
}

static bool event_matches_active_id(struct json_object *event, const char *active_event_id) {
    struct json_object *event_id;
    return json_object_object_get_ex(event, "event_id", &event_id) &&
           json_object_is_type(event_id, json_type_string) &&
           strcmp(json_object_get_string(event_id), active_event_id) == 0;
}

static bool load_event(struct json_object *selected_event, TrackConfig *track) {
    struct json_object *enabled;
    if (!json_object_object_get_ex(selected_event, "enabled", &enabled) ||
        !json_object_get_boolean(enabled)) {
        return false;
    }

    memset(track, 0, sizeof(*track));
    track->min_lap_time_seconds = 10;
    if (!copy_required_string(selected_event, "event_id", track->event_id, sizeof(track->event_id)) ||
        !copy_required_string(selected_event, "event_name", track->event_name, sizeof(track->event_name)) ||
        !copy_required_string(selected_event, "track_id", track->track_id, sizeof(track->track_id)) ||
        !copy_required_string(selected_event, "track_name", track->track_name, sizeof(track->track_name))) {
        fprintf(stderr, "Track config error: selected event has missing identity fields\n");
        return false;
    }

    struct json_object *lap_mode;
    if (!json_object_object_get_ex(selected_event, "lap_mode", &lap_mode) ||
        !json_object_is_type(lap_mode, json_type_string)) {
        fprintf(stderr, "Track config error: lap_mode is required\n");
        return false;
    }

    const char *mode = json_object_get_string(lap_mode);
    if (strcmp(mode, "separate") == 0) {
        track->lap_mode = TRACK_LAP_MODE_SEPARATE;
        if (!parse_polygon(selected_event, "start_polygon", &track->start_polygon) ||
            !parse_polygon(selected_event, "finish_polygon", &track->finish_polygon)) {
            fprintf(stderr, "Track config error: separate mode requires valid start_polygon and finish_polygon\n");
            return false;
        }
    } else if (strcmp(mode, "shared") == 0) {
        track->lap_mode = TRACK_LAP_MODE_SHARED;
        if (!parse_polygon(selected_event, "start_finish_polygon", &track->start_polygon)) {
            fprintf(stderr, "Track config error: shared mode requires a valid start_finish_polygon\n");
            return false;
        }
        track->finish_polygon = track->start_polygon;
    } else {
        fprintf(stderr, "Track config error: lap_mode must be 'separate' or 'shared'\n");
        return false;
    }

    struct json_object *min_lap_seconds;
    if (json_object_object_get_ex(selected_event, "min_lap_seconds", &min_lap_seconds)) {
        int seconds = json_object_get_int(min_lap_seconds);
        if (seconds > 0) track->min_lap_time_seconds = seconds;
    }

    struct json_object *min_speed_kmh;
    if (json_object_object_get_ex(selected_event, "min_speed_kmh", &min_speed_kmh)) {
        track->min_speed_kmh = (float)json_object_get_double(min_speed_kmh);
    }

    return true;
}

static void print_loaded_track(const TrackConfig *track, const char *selection_method) {
    printf("Track %s: %s / %s (%s mode, minimum lap %ds, minimum speed %.1f km/h)\n",
           selection_method, track->track_name, track->event_name,
           track->lap_mode == TRACK_LAP_MODE_SHARED ? "shared" : "separate",
           track->min_lap_time_seconds, track->min_speed_kmh);
}

bool track_config_load(const char *filename, TrackConfig *track) {
    struct json_object *root = json_object_from_file(filename);
    if (root == NULL) {
        fprintf(stderr, "Track config error: cannot load %s\n", filename);
        return false;
    }

    bool ok = false;
    struct json_object *active_event_id;
    struct json_object *events;
    if (!json_object_object_get_ex(root, "active_event_id", &active_event_id) ||
        !json_object_is_type(active_event_id, json_type_string) ||
        !json_object_object_get_ex(root, "events", &events) ||
        !json_object_is_type(events, json_type_array)) {
        fprintf(stderr, "Track config error: active_event_id and events are required\n");
        goto cleanup;
    }

    const char *active_id = json_object_get_string(active_event_id);
    size_t event_count = json_object_array_length(events);
    for (size_t i = 0; i < event_count; i++) {
        struct json_object *event = json_object_array_get_idx(events, i);
        if (json_object_is_type(event, json_type_object) && event_matches_active_id(event, active_id)) {
            ok = load_event(event, track);
            break;
        }
    }

    if (!ok) {
        fprintf(stderr, "Track config error: active event '%s' is missing, disabled, or invalid\n", active_id);
    } else {
        print_loaded_track(track, "loaded");
    }

cleanup:
    json_object_put(root);
    return ok;
}

bool track_config_load_by_position(const char *filename, double latitude,
                                   double longitude, TrackConfig *track) {
    struct json_object *root = json_object_from_file(filename);
    if (root == NULL) {
        fprintf(stderr, "Track config error: cannot load %s\n", filename);
        return false;
    }

    bool ok = false;
    struct json_object *events;
    if (!json_object_object_get_ex(root, "events", &events) ||
        !json_object_is_type(events, json_type_array)) {
        fprintf(stderr, "Track config error: events array is required\n");
        goto cleanup;
    }

    size_t event_count = json_object_array_length(events);
    for (size_t i = 0; i < event_count; i++) {
        struct json_object *event = json_object_array_get_idx(events, i);
        TrackConfig candidate;
        if (!json_object_is_type(event, json_type_object) || !load_event(event, &candidate)) {
            continue;
        }

        if (config_polygon_contains(&candidate.start_polygon, latitude, longitude) ||
            config_polygon_contains(&candidate.finish_polygon, latitude, longitude)) {
            *track = candidate;
            print_loaded_track(track, "auto-selected");
            ok = true;
            break;
        }
    }

cleanup:
    json_object_put(root);
    return ok;
}