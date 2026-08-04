#ifndef LAP_TIMER_H
#define LAP_TIMER_H

#include <stdbool.h>
#include <time.h>
#include "config_parser.h"
#include "gps_m9n.h"

typedef struct {
    Config config;
    bool enabled;
    bool timing_active;
    bool position_initialized;
    bool was_in_start;
    bool was_in_finish;
    float last_lap_time;
    struct timespec started_at;
} LapTimer;

void lap_timer_init(LapTimer *timer, const Config *config);
void lap_timer_update(LapTimer *timer, const GPSData *gps, float *lap_time_seconds,
                      bool *timing_active);

#endif // LAP_TIMER_H
