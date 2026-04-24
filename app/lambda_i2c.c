/*
 * Lambda Sensor I2C Driver - Implementation
 * Reads lambda value from Arduino I2C slave at address 0x33
 */

#include "lambda_i2c.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Configuration
#define I2C_BUS            "/dev/i2c-8"
#define LAMBDA_ADDR        0x33

// Global I2C file descriptor
static int i2c_fd = -1;

bool lambda_init() {
    // Open I2C bus
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C bus for Lambda");
        return false;
    }

    // Set I2C slave address
    if (ioctl(i2c_fd, I2C_SLAVE, LAMBDA_ADDR) < 0) {
        perror("Failed to set Lambda I2C address");
        close(i2c_fd);
        i2c_fd = -1;
        return false;
    }

    printf("Lambda sensor initialized on %s at 0x%02X\n", I2C_BUS, LAMBDA_ADDR);
    return true;
}

float lambda_read() {
    if (i2c_fd < 0) {
        return -1.0f; // Not initialized
    }

    // Read 6 bytes from Arduino (Wire.requestFrom protocol)
    // First 4 bytes: float value (little-endian IEEE 754)
    // Last 2 bytes: unused
    uint8_t buffer[6];
    
    if (read(i2c_fd, buffer, 6) != 6) {
        // Read error - return error value
        return -1.0f;
    }

    // Extract float from first 4 bytes (little-endian)
    union {
        uint8_t bytes[4];
        float value;
    } converter;
    
    converter.bytes[0] = buffer[0];
    converter.bytes[1] = buffer[1];
    converter.bytes[2] = buffer[2];
    converter.bytes[3] = buffer[3];

    return converter.value;
}

void lambda_close() {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}
