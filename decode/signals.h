#ifndef SIGNALS_H
#define SIGNALS_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "../app/gps_m9n.h"

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
    
    // Sensors
    float imu_temp;        // ISM330DHCXTR temperature sensor
    float g_force_lat;     // Lateral G-force (ISM330DHCXTR)
    float g_force_long;    // Longitudinal G-force (ISM330DHCXTR)
    float gyro_x;          // Gyroscope X (ISM330DHCXTR)
    float gyro_y;          // Gyroscope Y (ISM330DHCXTR)
    float gyro_z;          // Gyroscope Z (ISM330DHCXTR)
    float mag_x;           // Magnetometer X (MMC5983MA)
    float mag_y;           // Magnetometer Y (MMC5983MA)
    float mag_z;           // Magnetometer Z (MMC5983MA)
    int gps_satellites;    // Number of GPS satellites
    GPSData gps_data;      // GPS satellite data for sky plot
    
    // Racing telemetry
    float delta_time;      // Delta time vs reference lap (negative = faster)
    float current_lap_time; // Current lap time in seconds
    float reference_lap_time; // Reference lap time in seconds
    
    // Status
    bool can_connected;
} VehicleData;

extern VehicleData v_data;
extern pthread_mutex_t data_mutex;

void signals_init();
void signals_update_lambda();
void signals_calculate_gear(); // New Function

#endif
