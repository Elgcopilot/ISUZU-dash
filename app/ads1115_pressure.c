/*
 * ADS1115 Pressure Sensor Driver - Implementation
 * Reads pressure from ADS1115 AIN1 channel on I2C8
 */

#include "ads1115_pressure.h"
#include "../decode/signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

// Configuration
#define I2C_BUS            "/dev/i2c-8"
#define ADS1115_ADDR       0x48
#define SENSOR_CHANNEL     1              // AIN1

// ADS1115 Registers
#define REG_CONVERSION     0x00
#define REG_CONFIG         0x01

// Config register bits for AIN1, ±6.144V, single-shot, 128 SPS
// OS[15]=1 (start), MUX[14:12]=101 (AIN1), PGA[11:9]=000 (±6.144V)
// MODE[8]=1 (single), DR[7:5]=100 (128SPS), COMP[1:0]=11 (disable)
// High byte: 0xD1 = 1101 0001  Low byte: 0x83 = 1000 0011
#define CONFIG_AIN1        0xD183

// Sensor calibration
#define PMAX_PSI           100.0f         // Maximum pressure rating
#define SENSOR_VMIN        0.5f           // Sensor voltage at 0 PSI
#define SENSOR_VMAX        4.5f           // Sensor voltage at PMAX_PSI
#define SENSOR_VSPAN       4.0f           // (VMAX - VMIN)
#define PSI_TO_KPA         6.89476f       // Conversion factor
#define VOLTS_PER_BIT      0.0001875f     // 6.144V / 32768 (±6.144V range)

// Global I2C file descriptor
static int i2c_fd = -1;

bool ads1115_init() {
    // Open I2C bus
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) {
        perror("Failed to open I2C bus");
        return false;
    }

    // Set I2C slave address
    if (ioctl(i2c_fd, I2C_SLAVE, ADS1115_ADDR) < 0) {
        perror("Failed to set I2C address");
        close(i2c_fd);
        i2c_fd = -1;
        return false;
    }

    printf("ADS1115 initialized on %s at 0x%02X\n", I2C_BUS, ADS1115_ADDR);
    return true;
}

static int16_t ads1115_read_adc() {
    if (i2c_fd < 0) {
        return -32768; // Error value
    }

    // Write config to start conversion (lock only for the write)
    uint8_t config_data[3] = {
        REG_CONFIG,
        (CONFIG_AIN1 >> 8) & 0xFF,  // High byte
        CONFIG_AIN1 & 0xFF          // Low byte
    };

    pthread_mutex_lock(&i2c8_mutex);
    if (write(i2c_fd, config_data, 3) != 3) {
        perror("Failed to write config");
        pthread_mutex_unlock(&i2c8_mutex);
        return -32768;
    }
    pthread_mutex_unlock(&i2c8_mutex);

    // Wait for conversion OUTSIDE mutex (128 SPS → ~7.8ms, 10ms is safe)
    usleep(10000);

    // Read conversion result (lock only for pointer-write + read)
    uint8_t reg = REG_CONVERSION;
    uint8_t data[2];

    pthread_mutex_lock(&i2c8_mutex);
    if (write(i2c_fd, &reg, 1) != 1) {
        perror("Failed to set pointer");
        pthread_mutex_unlock(&i2c8_mutex);
        return -32768;
    }
    if (read(i2c_fd, data, 2) != 2) {
        perror("Failed to read conversion");
        pthread_mutex_unlock(&i2c8_mutex);
        return -32768;
    }
    pthread_mutex_unlock(&i2c8_mutex);

    // Combine to signed 16-bit value
    int16_t raw = (data[0] << 8) | data[1];
    return raw;
}

float ads1115_read_boost_kpa() {
    // Single fast read - spike rejection in main thread handles noise
    int16_t raw = ads1115_read_adc();
    if (raw == -32768) {
        return -1.0f; // Error
    }
    
    // Convert to voltage
    float voltage = raw * VOLTS_PER_BIT;
    
    // Prevent negative voltage
    if (voltage < 0.0f) voltage = 0.0f;
    
    // Datasheet transfer function (sealed gage, V_supply=5V):
    //   Output(V) = (0.8*Vs/(Pmax-Pmin)) * (P - Pmin) + 0.10*Vs
    // Inverted: P_PSI = (V - 0.5) * Pmax / 4.0
    float boostPSI = (voltage - 0.5f) * (PMAX_PSI / 4.0f);
    
    // Prevent negative readings from noise
    if (boostPSI < 0.0f) boostPSI = 0.0f;
    
    // Convert PSI to kPa (1 PSI = 6.89476 kPa)
    float boost_kpa = boostPSI * PSI_TO_KPA;
    
    return boost_kpa;
}

void ads1115_close() {
    if (i2c_fd >= 0) {
        close(i2c_fd);
        i2c_fd = -1;
    }
}
