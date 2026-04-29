#ifndef ISM330_IMU_H
#define ISM330_IMU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float accel_x;  // Lateral acceleration (G)
    float accel_y;  // Longitudinal acceleration (G)
    float accel_z;  // Vertical acceleration (G)
    float temp;     // Temperature (°C)
} IMUData;

// Initialize ISM330DHCXTR on I2C
bool imu_init(void);

// Read accelerometer and temperature data
void imu_update(IMUData *imu_data);

// Close I2C connection
void imu_close(void);

#endif
