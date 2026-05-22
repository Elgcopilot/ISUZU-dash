/*
 * MMC5983MA Magnetometer Driver - Implementation
 */

#include "mmc5983ma_mag.h"
#include "../decode/signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

// MMC5983MA I2C address
#define MMC5983MA_ADDR 0x30

// Register addresses (verified from working Python code)
#define MMC5983MA_XOUT0       0x00  // X[17:10], Y, Z data start here
#define MMC5983MA_STATUS      0x08  // Status register
#define MMC5983MA_CTRL0       0x09  // Control register 0 (TM_M trigger)
#define MMC5983MA_CTRL1       0x0A  // Control register 1 (SW_RST)
#define MMC5983MA_CTRL2       0x0B  // Control register 2
#define MMC5983MA_PRODUCT_ID  0x2F  // Product ID register

// Control register bits
#define MMC5983MA_CTRL0_TM_M  0x01  // Take measurement
#define MMC5983MA_CTRL0_TM_T  0x02  // Take temperature measurement  
#define MMC5983MA_CTRL0_START_MDT 0x04  // Start motion detection
#define MMC5983MA_CTRL0_SET   0x08  // SET operation
#define MMC5983MA_CTRL0_RESET 0x10  // RESET operation
#define MMC5983MA_CTRL0_AUTO_SR_EN 0x20  // Auto SET/RESET enable
#define MMC5983MA_CTRL0_AUTO_ST_EN 0x40  // Auto self-test enable
#define MMC5983MA_CTRL0_CMM_FREQ_EN 0x80  // Continuous mode frequency enable
#define MMC5983MA_CTRL1_BW0   0x01  // Bandwidth 100Hz
#define MMC5983MA_CTRL1_SW_RST 0x80  // Software reset
#define MMC5983MA_CTRL2_CMM_EN 0x10 // Continuous mode enable
#define MMC5983MA_CTRL2_EN_PRD_SET 0x08  // Enable periodic SET
#define MMC5983MA_CTRL2_CMM_FREQ_EN 0x80  // Continuous mode frequency enable bit in CTRL2
#define MMC5983MA_CTRL2_CM_FREQ_100HZ 0x04  // 100Hz continuous frequency
#define MMC5983MA_STATUS_MEAS_M_DONE 0x01  // Measurement done
#define MMC5983MA_STATUS_MEAS_T_DONE 0x02  // Temperature measurement done

static int mag_fd = -1;

// Write to register
static bool write_register(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    pthread_mutex_lock(&i2c8_mutex);
    bool ok = (write(mag_fd, buf, 2) == 2);
    pthread_mutex_unlock(&i2c8_mutex);
    return ok;
}

// Read from register
static bool read_register(uint8_t reg, uint8_t *value) {
    pthread_mutex_lock(&i2c8_mutex);
    bool ok = (write(mag_fd, &reg, 1) == 1) && (read(mag_fd, value, 1) == 1);
    pthread_mutex_unlock(&i2c8_mutex);
    return ok;
}

// Read multiple registers
static bool read_registers(uint8_t reg, uint8_t *buffer, uint8_t len) {
    pthread_mutex_lock(&i2c8_mutex);
    bool ok = (write(mag_fd, &reg, 1) == 1) && (read(mag_fd, buffer, len) == len);
    pthread_mutex_unlock(&i2c8_mutex);
    return ok;
}

bool mag_init(void) {
    FILE *log = fopen("/tmp/mag_debug.log", "w");
    if (log) {
        fprintf(log, "MMC5983MA magnetometer initialization starting...\n");
        fflush(log);
    }
    
    // Open I2C bus 8 (where MMC5983MA is connected)
    mag_fd = open("/dev/i2c-8", O_RDWR);
    if (mag_fd < 0) {
        printf("Failed to open I2C-8 bus for magnetometer: %s\n", strerror(errno));
        if (log) {
            fprintf(log, "Failed to open I2C-8: %s\n", strerror(errno));
            fclose(log);
        }
        return false;
    }

    if (log) {
        fprintf(log, "I2C-8 opened successfully (fd=%d)\n", mag_fd);
        fflush(log);
    }

    // Set I2C slave address
    if (ioctl(mag_fd, I2C_SLAVE, MMC5983MA_ADDR) < 0) {
        printf("Failed to set magnetometer I2C address: %s\n", strerror(errno));
        if (log) {
            fprintf(log, "Failed to set I2C address 0x%02X: %s\n", MMC5983MA_ADDR, strerror(errno));
            fclose(log);
        }
        close(mag_fd);
        mag_fd = -1;
        return false;
    }

    if (log) {
        fprintf(log, "I2C address set to 0x%02X\n", MMC5983MA_ADDR);
        fflush(log);
    }

    // Verify Product ID (should be 0x30 for MMC5983MA)
    uint8_t product_id = 0;
    if (!read_register(MMC5983MA_PRODUCT_ID, &product_id)) {
        printf("Failed to read magnetometer Product ID register\n");
        if (log) {
            fprintf(log, "Failed to read Product ID register\n");
            fclose(log);
        }
        close(mag_fd);
        mag_fd = -1;
        return false;
    }

    if (log) {
        fprintf(log, "Product ID read: 0x%02X (expected 0x30)\n", product_id);
        fflush(log);
    }

    if (product_id != 0x30) {
        printf("Magnetometer Product ID mismatch: expected 0x30, got 0x%02X\n", product_id);
        if (log) {
            fprintf(log, "Product ID mismatch!\n");
            fclose(log);
        }
        close(mag_fd);
        mag_fd = -1;
        return false;
    }

    // Perform software reset (matching Python code)
    if (log) {
        fprintf(log, "Performing software reset (CTRL1 = 0x80)...\n");
        fflush(log);
    }
    write_register(MMC5983MA_CTRL1, 0x80);
    usleep(100000);  // Wait 100ms for reset to complete

    if (log) {
        fprintf(log, "Magnetometer configured for single-shot TM_M mode\n");
        fflush(log);
    }

    printf("MMC5983MA magnetometer initialized successfully\n");
    if (log) {
        fprintf(log, "Initialization complete!\n");
        fclose(log);
    }
    return true;
}

void mag_close(void) {
    if (mag_fd >= 0) {
        close(mag_fd);
        mag_fd = -1;
    }
}

void mag_update(MagData *mag_data) {
    if (mag_fd < 0 || !mag_data) return;

    // Trigger single measurement (TM_M = 0x01, matching Python code)
    write_register(MMC5983MA_CTRL0, 0x01);
    
    // Wait for MEAS_M_DONE (bit 0 of STATUS register), timeout ~20ms
    uint8_t status = 0;
    int retries = 20;
    while (retries-- > 0) {
        if (read_register(MMC5983MA_STATUS, &status) && (status & MMC5983MA_STATUS_MEAS_M_DONE)) break;
        usleep(1000);
    }

    uint8_t data[6];

    // Read 6 bytes of magnetometer data (matching Python code)
    if (read_registers(MMC5983MA_XOUT0, data, 6)) {
        // Debug: log first read to see raw bytes
        static int debug_count = 0;
        if (debug_count < 3) {
            FILE *log = fopen("/tmp/mag_debug.log", "a");
            if (log) {
                fprintf(log, "DEBUG: Raw bytes: %02X %02X %02X %02X %02X %02X\n",
                        data[0], data[1], data[2], data[3], data[4], data[5]);
                fclose(log);
            }
            debug_count++;
        }
        
        // Combine bytes to form 18-bit values (matching Python code)
        // X = (data[0] << 10) | (data[1] << 2) | (data[2] >> 6)
        // Y = ((data[2] & 0x3F) << 12) | (data[3] << 4) | (data[4] >> 4)
        // Z = ((data[4] & 0x0F) << 14) | (data[5] << 6)
        uint32_t mag_x_raw = ((uint32_t)data[0] << 10) | ((uint32_t)data[1] << 2) | ((uint32_t)data[2] >> 6);
        uint32_t mag_y_raw = ((uint32_t)(data[2] & 0x3F) << 12) | ((uint32_t)data[3] << 4) | ((uint32_t)data[4] >> 4);
        uint32_t mag_z_raw = ((uint32_t)(data[4] & 0x0F) << 14) | ((uint32_t)data[5] << 6);

        // Convert to signed 18-bit (matching Python to_signed function)
        if (mag_x_raw & (1 << 17)) mag_x_raw -= (1 << 18);
        if (mag_y_raw & (1 << 17)) mag_y_raw -= (1 << 18);
        if (mag_z_raw & (1 << 17)) mag_z_raw -= (1 << 18);
        
        int32_t mag_x = (int32_t)mag_x_raw;
        int32_t mag_y = (int32_t)mag_y_raw;
        int32_t mag_z = (int32_t)mag_z_raw;

        // Convert to Gauss (use same scale as before)
        mag_data->mag_x = mag_x * 0.0000625f;
        mag_data->mag_y = mag_y * 0.0000625f;
        mag_data->mag_z = mag_z * 0.0000625f;
        
        // Also log converted values
        if (debug_count < 3) {
            FILE *log = fopen("/tmp/mag_debug.log", "a");
            if (log) {
                fprintf(log, "DEBUG: Raw values: X=%d Y=%d Z=%d\n", mag_x, mag_y, mag_z);
                fclose(log);
            }
        }
    } else {
        // On read failure, set to zero
        mag_data->mag_x = 0.0f;
        mag_data->mag_y = 0.0f;
        mag_data->mag_z = 0.0f;
    }
}
