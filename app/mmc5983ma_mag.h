/*
 * MMC5983MA Magnetometer Driver
 * 3-axis magnetometer on I2C-8 at address 0x30
 */

#ifndef MMC5983MA_MAG_H
#define MMC5983MA_MAG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float mag_x;  // Magnetic field X-axis (Gauss)
    float mag_y;  // Magnetic field Y-axis (Gauss)
    float mag_z;  // Magnetic field Z-axis (Gauss)
} MagData;

// Initialize MMC5983MA on I2C
bool mag_init(void);

// Read magnetometer data
void mag_update(MagData *mag_data);

// Close I2C connection
void mag_close(void);

#endif // MMC5983MA_MAG_H
