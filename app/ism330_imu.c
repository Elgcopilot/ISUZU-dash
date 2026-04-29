#include "ism330_imu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <errno.h>

// ISM330DHCXTR I2C address (SA0 = 1)
#define ISM330_ADDR 0x6B

// Register addresses
#define WHO_AM_I        0x0F
#define CTRL1_XL        0x10  // Accelerometer control
#define CTRL2_G         0x11  // Gyroscope control
#define OUT_TEMP_L      0x20  // Temperature output low
#define OUT_TEMP_H      0x21  // Temperature output high
#define OUTX_L_G        0x22  // Gyroscope X-axis low
#define OUTX_H_G        0x23  // Gyroscope X-axis high
#define OUTY_L_G        0x24  // Gyroscope Y-axis low
#define OUTY_H_G        0x25  // Gyroscope Y-axis high
#define OUTZ_L_G        0x26  // Gyroscope Z-axis low
#define OUTZ_H_G        0x27  // Gyroscope Z-axis high
#define OUTX_L_A        0x28  // Accelerometer X-axis low
#define OUTX_H_A        0x29  // Accelerometer X-axis high
#define OUTY_L_A        0x2A  // Accelerometer Y-axis low
#define OUTY_H_A        0x2B  // Accelerometer Y-axis high
#define OUTZ_L_A        0x2C  // Accelerometer Z-axis low
#define OUTZ_H_A        0x2D  // Accelerometer Z-axis high

static int imu_fd = -1;

// Write to register
static bool write_register(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = {reg, value};
    if (write(imu_fd, buf, 2) != 2) {
        return false;
    }
    return true;
}

// Read from register
static bool read_register(uint8_t reg, uint8_t *value) {
    if (write(imu_fd, &reg, 1) != 1) {
        return false;
    }
    if (read(imu_fd, value, 1) != 1) {
        return false;
    }
    return true;
}

// Read multiple registers
static bool read_registers(uint8_t reg, uint8_t *buffer, uint8_t len) {
    if (write(imu_fd, &reg, 1) != 1) {
        return false;
    }
    if (read(imu_fd, buffer, len) != len) {
        return false;
    }
    return true;
}

bool imu_init(void) {
    // Open I2C bus 8 (where ISM330DHCXTR is connected)
    imu_fd = open("/dev/i2c-8", O_RDWR);
    if (imu_fd < 0) {
        printf("Failed to open I2C-8 bus for IMU: %s\n", strerror(errno));
        return false;
    }

    // Set I2C slave address
    if (ioctl(imu_fd, I2C_SLAVE, ISM330_ADDR) < 0) {
        printf("Failed to set IMU I2C address: %s\n", strerror(errno));
        close(imu_fd);
        imu_fd = -1;
        return false;
    }

    // Verify WHO_AM_I (should be 0x6B for ISM330DHCXTR)
    uint8_t who_am_i = 0;
    if (!read_register(WHO_AM_I, &who_am_i)) {
        printf("Failed to read IMU WHO_AM_I register\n");
        close(imu_fd);
        imu_fd = -1;
        return false;
    }

    if (who_am_i != 0x6B) {
        printf("IMU WHO_AM_I mismatch: expected 0x6B, got 0x%02X\n", who_am_i);
        close(imu_fd);
        imu_fd = -1;
        return false;
    }

    // Configure accelerometer
    // CTRL1_XL: ODR=416Hz, ±2g full scale, LPF2 enabled
    if (!write_register(CTRL1_XL, 0x60)) {  // 0110 0000: 416Hz, ±2g
        printf("Failed to configure IMU accelerometer\n");
        close(imu_fd);
        imu_fd = -1;
        return false;
    }

    // Configure gyroscope
    // CTRL2_G: ODR=416Hz, ±250 dps full scale
    if (!write_register(CTRL2_G, 0x60)) {  // 0110 0000: 416Hz, ±250 dps
        printf("Failed to configure IMU gyroscope\n");
        close(imu_fd);
        imu_fd = -1;
        return false;
    }

    usleep(10000);  // Wait 10ms for sensor to stabilize

    printf("ISM330DHCXTR IMU initialized successfully (accel + gyro)\n");
    return true;
}

void imu_close(void) {
    if (imu_fd >= 0) {
        close(imu_fd);
        imu_fd = -1;
    }
}

void imu_update(IMUData *imu_data) {
    if (imu_fd < 0 || !imu_data) return;

    uint8_t accel_data[6];
    uint8_t gyro_data[6];
    uint8_t temp_data[2];

    // Read accelerometer data (6 bytes: X, Y, Z)
    if (read_registers(OUTX_L_A, accel_data, 6)) {
        // Combine low and high bytes (little-endian)
        int16_t accel_x_raw = (int16_t)((accel_data[1] << 8) | accel_data[0]);
        int16_t accel_y_raw = (int16_t)((accel_data[3] << 8) | accel_data[2]);
        int16_t accel_z_raw = (int16_t)((accel_data[5] << 8) | accel_data[4]);

        // Convert to G (±2g range, 16-bit resolution)
        // Sensitivity: 0.061 mg/LSB = 0.000061 g/LSB
        imu_data->accel_x = accel_x_raw * 0.000061f;
        imu_data->accel_y = accel_y_raw * 0.000061f;
        imu_data->accel_z = accel_z_raw * 0.000061f;
    }

    // Read gyroscope data (6 bytes: X, Y, Z)
    if (read_registers(OUTX_L_G, gyro_data, 6)) {
        // Combine low and high bytes (little-endian)
        int16_t gyro_x_raw = (int16_t)((gyro_data[1] << 8) | gyro_data[0]);
        int16_t gyro_y_raw = (int16_t)((gyro_data[3] << 8) | gyro_data[2]);
        int16_t gyro_z_raw = (int16_t)((gyro_data[5] << 8) | gyro_data[4]);

        // Convert to deg/s (±250 dps range, 16-bit resolution)
        // Sensitivity: 8.75 mdps/LSB = 0.00875 dps/LSB
        imu_data->gyro_x = gyro_x_raw * 0.00875f;
        imu_data->gyro_y = gyro_y_raw * 0.00875f;
        imu_data->gyro_z = gyro_z_raw * 0.00875f;
    }

    // Read temperature data (2 bytes)
    if (read_registers(OUT_TEMP_L, temp_data, 2)) {
        int16_t temp_raw = (int16_t)((temp_data[1] << 8) | temp_data[0]);
        // Temperature = (temp_raw / 256.0) + 25.0°C
        imu_data->temp = (temp_raw / 256.0f) + 25.0f;
    }
}
