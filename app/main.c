#define _GNU_SOURCE
#include "lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "ui.h"
#include "styles.h"
#include "can_mgr.h"
#include "signals.h"
#include "ads1115_pressure.h"
#include "lambda_i2c.h"
#include "gps_m9n.h"
#include "ism330_imu.h"
#include "mmc5983ma_mag.h"
#include "mqtt_client.h"
#include "config_parser.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include "app_config.h"

#define DISP_BUF_SIZE (800 * 480 / 10)

// ADS1115 Pressure Sensor Thread
void *ads1115_thread(void *arg) {
    (void)arg;
    
    // Initialize ADS1115
    if (!ads1115_init()) {
        printf("Failed to initialize ADS1115, pressure sensor disabled\n");
        return NULL;
    }
    
    printf("ADS1115 pressure sensor thread started\n");
    
    while(1) {
        // Read boost pressure from ADS1115 AIN1
        float boost_kpa = ads1115_read_boost_kpa();
        
        if (boost_kpa >= 0.0f) {
            // Update boost value directly - UI smoothing handles variations
            pthread_mutex_lock(&data_mutex);
            v_data.boost = (int)(boost_kpa + 0.5f);  // Round to nearest int
            pthread_mutex_unlock(&data_mutex);
        }
        
        // Read at 20 Hz (50ms interval)
        usleep(50000);
    }
    
    ads1115_close();
    return NULL;
}

// Lambda Sensor I2C Thread
void *lambda_thread(void *arg) {
    (void)arg;
    
    // Initialize Lambda I2C sensor
    if (!lambda_init()) {
        printf("Failed to initialize Lambda sensor, lambda disabled\n");
        return NULL;
    }
    
    printf("Lambda sensor thread started\n");
    
    while(1) {
        // Read lambda value from Arduino I2C slave
        float lambda_value = lambda_read();
        
        if (lambda_value >= 0.0f) {
            // Update lambda value directly
            pthread_mutex_lock(&data_mutex);
            v_data.lambda = lambda_value;
            pthread_mutex_unlock(&data_mutex);
        }
        
        // Read at 10 Hz (100ms interval)
        usleep(100000);
    }
    
    lambda_close();
    return NULL;
}

// GPS M9N UART Thread
void *gps_thread(void *arg) {
    (void)arg;
    
    // Initialize GPS M9N
    if (!gps_init()) {
        printf("Failed to initialize GPS M9N, GPS disabled\n");
        return NULL;
    }
    
    printf("GPS M9N thread started\n");
    
    while(1) {
        // Read GPS data from UART4
        GPSData temp_gps = {0};  // Initialize to zero
        gps_update(&temp_gps);
        
        // Update global GPS data
        pthread_mutex_lock(&data_mutex);
        v_data.gps_data = temp_gps;
        v_data.gps_satellites = temp_gps.satellites_used;
        pthread_mutex_unlock(&data_mutex);
        
        // Read at 5 Hz (200ms interval)
        usleep(200000);
    }
    
    gps_close();
    return NULL;
}

// ISM330DHCXTR IMU Thread
void *imu_thread(void *arg) {
    (void)arg;
    
    FILE *log = fopen("/tmp/imu_debug.log", "w");
    if (log) {
        fprintf(log, "IMU thread started\n");
        fflush(log);
    }
    
    // Initialize ISM330DHCXTR
    if (!imu_init()) {
        if (log) {
            fprintf(log, "Failed to initialize ISM330DHCXTR IMU\n");
            fclose(log);
        }
        printf("Failed to initialize ISM330DHCXTR IMU, accelerometer disabled\n");
        return NULL;
    }
    
    if (log) {
        fprintf(log, "ISM330DHCXTR IMU initialized successfully\n");
        fflush(log);
    }
    printf("ISM330DHCXTR IMU thread started\n");
    
    int sample_count = 0;
    while(1) {
        // Read IMU data
        IMUData temp_imu;
        imu_update(&temp_imu);
        
        // Update global vehicle data
        pthread_mutex_lock(&data_mutex);
        v_data.g_force_lat = temp_imu.accel_x;   // Lateral acceleration
        v_data.g_force_long = temp_imu.accel_y;  // Longitudinal acceleration
        v_data.gyro_x = temp_imu.gyro_x;         // Gyroscope X
        v_data.gyro_y = temp_imu.gyro_y;         // Gyroscope Y
        v_data.gyro_z = temp_imu.gyro_z;         // Gyroscope Z
        v_data.imu_temp = temp_imu.temp;         // IMU temperature
        pthread_mutex_unlock(&data_mutex);
        
        // Log first 5 samples for debugging
        if (log && sample_count < 5) {
            fprintf(log, "Sample %d: X=%.3f Y=%.3f Z=%.3f T=%.1f\n", 
                    sample_count, temp_imu.accel_x, temp_imu.accel_y, temp_imu.accel_z, temp_imu.temp);
            fflush(log);
            sample_count++;
        }
        
        // Read at 50 Hz (20ms interval)
        usleep(20000);
    }
    
    if (log) fclose(log);
    imu_close();
    return NULL;
}

// MMC5983MA Magnetometer Thread
void *mag_thread(void *arg) {
    (void)arg;
    
    // Initialize MMC5983MA (will create /tmp/mag_debug.log)
    if (!mag_init()) {
        printf("Failed to initialize MMC5983MA magnetometer, mag disabled\n");
        return NULL;
    }
    
    printf("MMC5983MA magnetometer thread started\n");
    
    // Append to log file for data samples
    FILE *log = fopen("/tmp/mag_debug.log", "a");
    if (log) {
        fprintf(log, "\n=== Starting data acquisition ===\n");
        fflush(log);
    }
    
    int sample_count = 0;
    while(1) {
        // Read magnetometer data
        MagData temp_mag;
        mag_update(&temp_mag);
        
        // Update global vehicle data
        pthread_mutex_lock(&data_mutex);
        v_data.mag_x = temp_mag.mag_x;
        v_data.mag_y = temp_mag.mag_y;
        v_data.mag_z = temp_mag.mag_z;
        pthread_mutex_unlock(&data_mutex);
        
        // Log first 5 samples for debugging
        if (log && sample_count < 5) {
            fprintf(log, "Sample %d: X=%.3f Y=%.3f Z=%.3f Gauss\n", 
                    sample_count, temp_mag.mag_x, temp_mag.mag_y, temp_mag.mag_z);
            fflush(log);
            sample_count++;
        }
        
        // Read at 50 Hz (20ms interval)
        usleep(20000);
    }
    
    if (log) fclose(log);
    mag_close();
    return NULL;
}

// MQTT Telemetry Thread
void *mqtt_thread(void *arg) {
    (void)arg;
    
    FILE *log = fopen("/tmp/mqtt_debug.log", "w");
    if (log) {
        fprintf(log, "MQTT thread starting...\n");
        fflush(log);
    }
    
    // Load configuration
    Config config;
    if (log) {
        fprintf(log, "MQTT: Loading config from /mnt/candata/config.txt\n");
        fflush(log);
    }
    
    if (!config_load("/mnt/candata/config.txt", &config)) {
        if (log) {
            fprintf(log, "Failed to load config.txt, MQTT disabled\n");
            fclose(log);
        }
        printf("Failed to load config.txt, MQTT disabled\n");
        fflush(stdout);
        return NULL;
    }
    
    if (log) {
        fprintf(log, "MQTT: Config loaded successfully\n");
        fprintf(log, "Server: %s:%d\n", config.server, config.port);
        fflush(log);
    }
    
    config_print(&config);
    
    // Initialize MQTT client
    if (log) {
        fprintf(log, "MQTT: Initializing client...\n");
        fflush(log);
    }
    
    if (!mqtt_init(&config)) {
        if (log) {
            fprintf(log, "Failed to initialize MQTT, telemetry disabled\n");
            fclose(log);
        }
        printf("Failed to initialize MQTT, telemetry disabled\n");
        return NULL;
    }
    
    if (log) {
        fprintf(log, "MQTT telemetry thread started successfully\n");
        fflush(log);
    }
    printf("MQTT telemetry thread started\n");
    
    // Publish telemetry at 25 Hz (every 40ms)
    int publish_count = 0;
    while(1) {
        // Copy vehicle data with mutex protection
        VehicleData local_data;
        pthread_mutex_lock(&data_mutex);
        local_data = v_data;
        pthread_mutex_unlock(&data_mutex);
        
        // Publish to MQTT broker
        if (mqtt_is_connected()) {
            if (mqtt_publish_telemetry(&local_data, &config)) {
                publish_count++;
                if (log && (publish_count % 100 == 0)) {
                    fprintf(log, "MQTT: Published %d messages\n", publish_count);
                    fflush(log);
                }
            } else {
                if (log) {
                    fprintf(log, "MQTT: Failed to publish telemetry\n");
                    fflush(log);
                }
                printf("MQTT: Failed to publish telemetry\n");
            }
        } else {
            // Try to reconnect
            if (log) {
                fprintf(log, "MQTT: Reconnecting...\n");
                fflush(log);
            }
            printf("MQTT: Reconnecting...\n");
            mqtt_cleanup();
            sleep(5);
            mqtt_init(&config);
        }
        
        // Publish at 25 Hz (40ms interval)
        usleep(40000);
    }
    
    if (log) fclose(log);
    mqtt_cleanup();
    return NULL;
}

int main(void)
{
    // 1. PIN UI THREAD TO CPU CORE 0
    // This prevents the heavy UI drawing from interrupting the CAN processing on Cores 1/2
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    // 2. LVGL INITIALIZATION
    lv_init();
    fbdev_init();

    // 3. DISPLAY BUFFER SETUP
    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    // 4. DISPLAY DRIVER SETUP
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    lv_disp_drv_register(&disp_drv);

    // 5. INPUT SETUP (Optional - Uncomment if using touchscreen)
    /*
    evdev_init();
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
    */

    // 6. LOGIC & UI INIT
    signals_init();
    ui_init();

    // 7. START DATA THREADS
    pthread_t rx_th, tx_th, ads_th, lambda_th, gps_th, imu_th, mag_th, mqtt_th;
    
    // Start ADS1115 Pressure Sensor Thread (always runs)
    pthread_create(&ads_th, NULL, ads1115_thread, NULL);
    pthread_setname_np(ads_th, "ads1115");
    printf("ADS1115 pressure sensor thread started\n");
    
    // Start Lambda Sensor Thread (always runs)
    pthread_create(&lambda_th, NULL, lambda_thread, NULL);
    pthread_setname_np(lambda_th, "lambda");
    printf("Lambda sensor thread started\n");
    
    // Start GPS M9N Thread (always runs)
    pthread_create(&gps_th, NULL, gps_thread, NULL);
    pthread_setname_np(gps_th, "gps_m9n");
    printf("GPS M9N thread started\n");
    
    // Start ISM330DHCXTR IMU Thread (always runs)
    pthread_create(&imu_th, NULL, imu_thread, NULL);
    pthread_setname_np(imu_th, "ism330_imu");
    printf("ISM330DHCXTR IMU thread started\n");
    
    // Start MMC5983MA Magnetometer Thread (always runs)
    pthread_create(&mag_th, NULL, mag_thread, NULL);
    pthread_setname_np(mag_th, "mmc5983ma");
    printf("MMC5983MA magnetometer thread started\n");
    
    // Start MQTT Telemetry Thread (always runs)
    pthread_create(&mqtt_th, NULL, mqtt_thread, NULL);
    pthread_setname_np(mqtt_th, "mqtt_pub");
    printf("MQTT telemetry thread started\n");
    
    // Check if the CAN interface exists in the system
    if(access("/sys/class/net/can0", F_OK) == 0) {
        printf("--- STARTING REAL CAN MODE ---\n");
        can_init();
        
        // Start RX Thread (Listening)
        pthread_create(&rx_th, NULL, can_rx_thread, NULL);
        pthread_setname_np(rx_th, "can_rx");

        // Start TX Thread (OBD Polling)
        pthread_create(&tx_th, NULL, can_tx_obd_thread, NULL);
        pthread_setname_np(tx_th, "can_tx");
        
    } else {
        printf("--- STARTING SIMULATOR MODE ---\n");
        // Start Simulator (Generates Sine Wave Data)
        pthread_create(&rx_th, NULL, simulator_thread, NULL);
        pthread_setname_np(rx_th, "sim_data");
    }

    printf("UI Loop Started on Core 0.\n");

    // 8. ENGINE-OFF SHUTDOWN DETECTION (commented out)
    // int engine_off_ticks = 0;
    // const int shutdown_ticks = (SHUTDOWN_DELAY_SEC * 1000000) / 16000;
    // bool shutdown_triggered = false;

    // 9. MAIN UI LOOP
    while(1) {
        // Draw the screen
        lv_timer_handler();
        
        // Fetch new data and update labels/gauges
        ui_update();
        
        // Engine-off shutdown: disabled
        // if (!shutdown_triggered) {
        //     pthread_mutex_lock(&data_mutex);
        //     int current_rpm = v_data.rpm;
        //     bool connected = v_data.can_connected;
        //     pthread_mutex_unlock(&data_mutex);
        //
        //     if (connected && current_rpm == 0) {
        //         engine_off_ticks++;
        //         if (engine_off_ticks >= shutdown_ticks) {
        //             printf("ENGINE OFF for %d seconds — shutting down.\n", SHUTDOWN_DELAY_SEC);
        //             shutdown_triggered = true;
        //             system("shutdown -h now");
        //         }
        //     } else {
        //         engine_off_ticks = 0;
        //     }
        // }

        // Advance LVGL internal time by 16ms
        lv_tick_inc(16);
        
        // Sleep to maintain ~60 FPS for smooth gauge animation
        usleep(16000);

        // Voluntary yield to ensure kernel scheduler runs smoothly
        sched_yield();
    }

    return 0;
}