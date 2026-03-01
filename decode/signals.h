#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct {
    // Page 1
    float map_pressure;
    float lambda;
    float duty_injection; // From 0x151
    float fuel_rail_press;
    int coolant_temp;
    int oil_temp;

    // Page 2
    float oil_pressure;
    int rpm;
    int speed_obd;
    int pedal_pos;
    int brake_pos;
    int boost;          // Changed to allow negative (vacuum)
    int intake_temp;    // New: 0x0F
    int gear;           // New: Calculated Gear

    // Wheel Speeds (0x162)
    float speed_fl;
    float speed_fr;
    float speed_rl;
    float speed_rr;
    float speed_avg;    // Average of 4 wheels

    // Internal Calculations
    float maf;
    float fuel_rate;
    float battery_voltage; // 12V battery (OBD PID 0x42)
    
    // Status
    bool can_connected;
} VehicleData;

extern VehicleData v_data;
extern pthread_mutex_t data_mutex;

void signals_init();
void signals_update_lambda();
void signals_calculate_gear(); // New Function

#endif
