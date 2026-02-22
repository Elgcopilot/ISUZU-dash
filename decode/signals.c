#include "signals.h"
#include <string.h>
#include <math.h>

VehicleData v_data;
pthread_mutex_t data_mutex;

void signals_init() {
    pthread_mutex_init(&data_mutex, NULL);
    memset(&v_data, 0, sizeof(VehicleData));
    v_data.lambda = 1.0f; 
}

void signals_update_lambda() {
    if (v_data.fuel_rate > 0.1f) {
        float fuel_g_s = (v_data.fuel_rate * 835.0f) / 3600.0f;
        if (fuel_g_s > 0.0001f) {
            float afr = v_data.maf / fuel_g_s;
            v_data.lambda = afr / 14.5f;
            if (v_data.lambda > 10.0f) v_data.lambda = 10.0f;
        }
    } else {
        v_data.lambda = 10.0f;
    }
}

void signals_calculate_gear() {
    if (v_data.speed_avg < 2.0f || v_data.rpm < 500) {
        v_data.gear = 0;
        return;
    }

    float ratio = (float)v_data.rpm / v_data.speed_avg;

    // D-Max 1.9 Ratios
    if (ratio > 130.0f) v_data.gear = 1;
    else if (ratio > 80.0f) v_data.gear = 2;
    else if (ratio > 55.0f) v_data.gear = 3;
    else if (ratio > 40.0f) v_data.gear = 4;
    else if (ratio > 30.0f) v_data.gear = 5;
    else if (ratio > 20.0f) v_data.gear = 6;
    else v_data.gear = 0;
}