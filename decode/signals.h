#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    // Engine Data
    int rpm;
    int coolant_temp;
    int oil_temp;
    float oil_pressure;
    float fuel_rail_press;
    float duty_injection;
    float lambda;

    // Driving Data
    int speed_obd;
    int gear;
    int pedal_pos;
    int brake_pos;
    int boost;        // Allows negative for vacuum
    int intake_temp;

    // Wheel Speeds
    float speed_fl;
    float speed_fr;
    float speed_rl;
    float speed_rr;
    float speed_avg;

    // Internal
    float maf;
    float fuel_rate;
    bool can_connected;
} VehicleData;

extern VehicleData v_data;
extern pthread_mutex_t data_mutex;

void signals_init();
void signals_update_lambda();
void signals_calculate_gear();

#endif