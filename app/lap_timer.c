#include "lap_timer.h"

#include <stdio.h>
#include <string.h>

static float elapsed_seconds(const struct timespec *start, const struct timespec *now) {
    return (float)(now->tv_sec - start->tv_sec) +
           (float)(now->tv_nsec - start->tv_nsec) / 1000000000.0f;
}

static void record_completed_lap(LapTimer *timer, float lap_time) {
    timer->last_lap_time = lap_time;
    timer->last_lap_delta_valid = timer->best_lap_time > 0.0f;
    timer->last_lap_delta = timer->last_lap_delta_valid ?
                            lap_time - timer->best_lap_time : 0.0f;

    if (timer->best_lap_time <= 0.0f || lap_time < timer->best_lap_time) {
        timer->best_lap_time = lap_time;
    }
}

void lap_timer_init(LapTimer *timer, const TrackConfig *track) {
    memset(timer, 0, sizeof(*timer));
    timer->track = *track;
    timer->enabled = track->start_polygon.point_count == LAP_POLYGON_POINTS &&
                     track->finish_polygon.point_count == LAP_POLYGON_POINTS;

    if (timer->enabled) {
        printf("Lap timer enabled for %s / %s\n", track->track_name, track->event_name);
    } else {
        printf("Lap timer disabled: START and FINISH polygons require four points\n");
    }
}

void lap_timer_switch_event(LapTimer *timer, const TrackConfig *track) {
    lap_timer_init(timer, track);

    // Selection happens as the vehicle enters a configured lap gate. Treat
    // that entry as a real crossing so the first lap starts immediately.
    timer->position_initialized = true;
}

void lap_timer_update(LapTimer *timer, const GPSData *gps, float *lap_time_seconds,
                      bool *timing_active, float *best_lap_time, float *lap_delta,
                      bool *lap_delta_valid) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    *lap_time_seconds = timer->last_lap_time;
    if (timer->timing_active) {
        *lap_time_seconds = elapsed_seconds(&timer->started_at, &now);
    }
    *timing_active = timer->timing_active;
    *best_lap_time = timer->best_lap_time;
    *lap_delta = timer->last_lap_delta;
    *lap_delta_valid = timer->last_lap_delta_valid;

    if (!timer->enabled || !gps->fix_valid || !gps->speed_valid ||
        gps->speed_kmh < timer->track.min_speed_kmh) return;

    bool in_start = config_polygon_contains(&timer->track.start_polygon,
                                            gps->latitude, gps->longitude);
    bool in_finish = config_polygon_contains(&timer->track.finish_polygon,
                                             gps->latitude, gps->longitude);

    // Establish the initial location without treating an application start inside a
    // zone as a crossing.
    if (!timer->position_initialized) {
        timer->was_in_start = in_start;
        timer->was_in_finish = in_finish;
        timer->position_initialized = true;
        return;
    }

    bool entered_start = !timer->was_in_start && in_start;
    bool entered_finish = !timer->was_in_finish && in_finish;

    if (timer->track.lap_mode == TRACK_LAP_MODE_SHARED && entered_start) {
        if (!timer->timing_active) {
            timer->started_at = now;
            timer->timing_active = true;
            timer->last_lap_time = 0.0f;
            *lap_time_seconds = 0.0f;
            *timing_active = true;
            printf("Lap timer: started\n");
        } else {
            float lap_time = elapsed_seconds(&timer->started_at, &now);
            if (lap_time >= (float)timer->track.min_lap_time_seconds) {
                record_completed_lap(timer, lap_time);
                timer->started_at = now;
                *lap_time_seconds = 0.0f;
                *best_lap_time = timer->best_lap_time;
                *lap_delta = timer->last_lap_delta;
                *lap_delta_valid = timer->last_lap_delta_valid;
                printf("Lap timer: completed shared lap in %.2f seconds; next lap started\n", lap_time);
            }
        }
    } else if (!timer->timing_active && entered_start) {
        timer->started_at = now;
        timer->timing_active = true;
        timer->last_lap_time = 0.0f;
        *lap_time_seconds = 0.0f;
        *timing_active = true;
        printf("Lap timer: started\n");
    } else if (timer->timing_active && entered_finish) {
        float lap_time = elapsed_seconds(&timer->started_at, &now);
        if (lap_time >= (float)timer->track.min_lap_time_seconds) {
            timer->timing_active = false;
            record_completed_lap(timer, lap_time);
            *lap_time_seconds = lap_time;
            *timing_active = false;
            *best_lap_time = timer->best_lap_time;
            *lap_delta = timer->last_lap_delta;
            *lap_delta_valid = timer->last_lap_delta_valid;
            printf("Lap timer: finished in %.2f seconds\n", lap_time);
        }
    }

    timer->was_in_start = in_start;
    timer->was_in_finish = in_finish;
}
