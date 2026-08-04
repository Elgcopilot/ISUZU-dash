#include "lap_timer.h"

#include <stdio.h>
#include <string.h>

static float elapsed_seconds(const struct timespec *start, const struct timespec *now) {
    return (float)(now->tv_sec - start->tv_sec) +
           (float)(now->tv_nsec - start->tv_nsec) / 1000000000.0f;
}

void lap_timer_init(LapTimer *timer, const Config *config) {
    memset(timer, 0, sizeof(*timer));
    timer->config = *config;
    timer->enabled = config->start_polygon.point_count == LAP_POLYGON_POINTS &&
                     config->finish_polygon.point_count == LAP_POLYGON_POINTS;

    if (timer->enabled) {
        printf("Lap timer enabled (minimum lap time: %d seconds)\n",
               timer->config.min_lap_time_seconds);
    } else {
        printf("Lap timer disabled: START and FINISH polygons require four points\n");
    }
}

void lap_timer_update(LapTimer *timer, const GPSData *gps, float *lap_time_seconds,
                      bool *timing_active) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    *lap_time_seconds = timer->last_lap_time;
    if (timer->timing_active) {
        *lap_time_seconds = elapsed_seconds(&timer->started_at, &now);
    }
    *timing_active = timer->timing_active;

    if (!timer->enabled || !gps->fix_valid) return;

    bool in_start = config_polygon_contains(&timer->config.start_polygon,
                                            gps->latitude, gps->longitude);
    bool in_finish = config_polygon_contains(&timer->config.finish_polygon,
                                             gps->latitude, gps->longitude);

    // Establish the initial location without treating an application start inside a
    // zone as a crossing.
    if (!timer->position_initialized) {
        timer->was_in_start = in_start;
        timer->was_in_finish = in_finish;
        timer->position_initialized = true;
        return;
    }

    if (!timer->timing_active && !timer->was_in_start && in_start) {
        timer->started_at = now;
        timer->timing_active = true;
        timer->last_lap_time = 0.0f;
        *lap_time_seconds = 0.0f;
        *timing_active = true;
        printf("Lap timer: started\n");
    } else if (timer->timing_active && !timer->was_in_finish && in_finish) {
        float lap_time = elapsed_seconds(&timer->started_at, &now);
        if (lap_time >= (float)timer->config.min_lap_time_seconds) {
            timer->timing_active = false;
            timer->last_lap_time = lap_time;
            *lap_time_seconds = lap_time;
            *timing_active = false;
            printf("Lap timer: finished in %.2f seconds\n", lap_time);
        }
    }

    timer->was_in_start = in_start;
    timer->was_in_finish = in_finish;
}
