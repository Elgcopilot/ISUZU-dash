#ifndef GPS_M9N_H
#define GPS_M9N_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_SATELLITES 32

typedef struct {
    int prn;          // Satellite PRN number
    int azimuth;      // 0-360 degrees
    int elevation;    // 0-90 degrees
    int cn0;          // Signal strength (C/N0) in dBHz
    bool used;        // Used in position fix
} Satellite;

typedef struct {
    int num_satellites;
    int satellites_used;
    Satellite sats[MAX_SATELLITES];
    
    // Position and time
    double latitude;   // Decimal degrees
    double longitude;  // Decimal degrees
    double speed_kmh;  // Ground speed from RMC, kilometres per hour
    int utc_hour;      // UTC time
    int utc_minute;
    int utc_second;
    bool time_valid;   // Time data valid
    bool fix_valid;    // GPS position fix status
    bool speed_valid;  // RMC reports a valid ground speed
} GPSData;

// Initialize GPS UART (UART4, 38400 baud)
bool gps_init(void);

// Read and parse NMEA sentences
void gps_update(GPSData *gps_data);

// Close GPS UART
void gps_close(void);

#endif
