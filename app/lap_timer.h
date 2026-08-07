#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <stdbool.h>
#include <time.h>
#include "gps_m9n.h"
#include "track_config.h"

typedef struct {
    TrackConfig track;
    bool enabled;
    bool timing_active;
    bool position_initialized;
    bool was_in_start;
    bool was_in_finish;
    float last_lap_time;
    float best_lap_time;
    float last_lap_delta;
    bool last_lap_delta_valid;
    struct timespec started_at;
} LapTimer;

void lap_timer_init(LapTimer *timer, const TrackConfig *track);
void lap_timer_switch_event(LapTimer *timer, const TrackConfig *track);
void lap_timer_update(LapTimer *timer, const GPSData *gps, float *lap_time_seconds,
                      bool *timing_active, float *best_lap_time, float *lap_delta,
                      bool *lap_delta_valid);

#endif // LAP_TIMER_H
