/*
 * Lambda Sensor I2C Driver - Header
 * Reads lambda value from Arduino I2C slave at address 0x33
 */

#ifndef LAMBDA_I2C_H
#define LAMBDA_I2C_H

#include <stdbool.h>

// Initialize Lambda I2C sensor
bool lambda_init();

// Read lambda value (returns -1.0f on error)
float lambda_read();

// Close Lambda I2C connection
void lambda_close();

#endif // LAMBDA_I2C_H
