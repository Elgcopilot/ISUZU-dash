/*
 * ADS1115 Pressure Sensor Driver
 * Reads pressure from ADS1115 AIN1 channel on I2C8
 * 
 * Hardware: ADS1115 at 0x48, Channel AIN1
 * Sensor: 0-100 PSI pressure sensor (0.5V = 0 PSI, 4.5V = 100 PSI)
 * Output: Boost pressure in kPa
 */

#ifndef ADS1115_PRESSURE_H
#define ADS1115_PRESSURE_H

#include <stdint.h>
#include <stdbool.h>

// Initialize ADS1115 on I2C8
bool ads1115_init();

// Read boost pressure in kPa from AIN1
// Returns pressure value, or -1.0f on error
float ads1115_read_boost_kpa();

// Close I2C bus
void ads1115_close();

#endif // ADS1115_PRESSURE_H
