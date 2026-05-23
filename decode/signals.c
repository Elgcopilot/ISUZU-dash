#include "signals.h"
#include <string.h>
#include <math.h>

VehicleData v_data;
pthread_mutex_t data_mutex;
pthread_mutex_t i2c8_mutex;

void signals_init() {
    pthread_mutex_init(&data_mutex, NULL);
    pthread_mutex_init(&i2c8_mutex, NULL);
    memset(&v_data, 0, sizeof(VehicleData));
    
    // Initialize Lambda to 0.0 so the gauge starts empty
    v_data.lambda = 0.0f; 
}

void signals_update_lambda() {
    // Logic ported from your Arduino code:
    // fuelRate is in L/h (polled from 0x5E)
    // maf is in g/s (polled from 0x10)
    
    if (v_data.fuel_rate > 0.1f) {
        // Density of Diesel approx 835 g/L
        float fuel_g_s = (v_data.fuel_rate * 835.0f) / 3600.0f; 
        
        if (fuel_g_s > 0.0001f) {
            float afr = v_data.maf / fuel_g_s;
            v_data.lambda = afr / 14.5f; // Diesel stoichiometric ratio
            
            // Clamp value to max 10.0
            if (v_data.lambda > 10.0f) v_data.lambda = 10.0f;
        }
    } else {
        // If engine is off or coasting (0 fuel), set Lambda to 0.0
        // This ensures the gauge bar drops to empty.
        v_data.lambda = 0.0f; 
    }
}

void signals_calculate_gear() {
    // Prevent division by zero or noise at standstill
    if (v_data.speed_obd < 2 || v_data.rpm < 500) {
        v_data.gear = 0; // Neutral
        return;
    }

    // Ratio = RPM / Speed (km/h)
    float ratio = (float)v_data.rpm / (float)v_data.speed_obd;

    // Thresholds based on your Isuzu D-Max data
    if (ratio > 130.0f) v_data.gear = 1;
    else if (ratio > 80.0f) v_data.gear = 2;
    else if (ratio > 55.0f) v_data.gear = 3;
    else if (ratio > 40.0f) v_data.gear = 4;
    else if (ratio > 30.0f) v_data.gear = 5;
    else if (ratio > 20.0f) v_data.gear = 6;
    else v_data.gear = 0;
}