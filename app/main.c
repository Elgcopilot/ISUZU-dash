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
#include "csv_logger.h"
#include "lap_timer.h"
#include "track_config.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include "app_config.h"

#define DISP_BUF_SIZE (800 * 480 / 10)
#define GPS_POLL_INTERVAL_US 10000  // Drain 25 Hz receiver output without delay.

static volatile sig_atomic_t shutdown_requested;

static void handle_shutdown_signal(int signal_number) {
    (void)signal_number;
    shutdown_requested = 1;
}

static bool install_signal_handlers(void) {
    struct sigaction action = {0};
    action.sa_handler = handle_shutdown_signal;
    sigemptyset(&action.sa_mask);

    return sigaction(SIGTERM, &action, NULL) == 0 &&
           sigaction(SIGINT, &action, NULL) == 0;
}

// ADS1115 Pressure Sensor Thread
void *ads1115_thread(void *arg) {
    (void)arg;
    
    // Initialize ADS1115
    if (!ads1115_init()) {
        printf("Failed to initialize ADS1115, pressure sensor disabled\n");
        return NULL;
    }
    
    printf("ADS1115 pressure sensor thread started\n");
    
    while(!shutdown_requested) {
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
    GPSData temp_gps = {0};
    TrackConfig track_config;
    LapTimer lap_timer;
    bool lap_timer_configured = false;
    bool track_selected = false;
    
    while(1) {
        // Read GPS data from UART4
        bool navigation_updated = gps_update(&temp_gps);
        if (!navigation_updated) {
            usleep(GPS_POLL_INTERVAL_US);
            continue;
        }

        // Lock to the first enabled event whose start/finish zone the vehicle
        // enters. This removes the need to edit active_event_id per event.
        if (!track_selected && temp_gps.fix_valid) {
            if (track_config_load_by_position("/mnt/candata/config/track.json",
                                              temp_gps.latitude, temp_gps.longitude,
                                              &track_config)) {
                lap_timer_switch_event(&lap_timer, &track_config);
                lap_timer_configured = true;
                track_selected = true;
                printf("Lap timer: automatically selected event '%s'\n", track_config.event_id);
            }
        }

        float lap_time = 0.0f;
        float best_lap_time = 0.0f;
        float lap_delta = 0.0f;
        bool lap_timing_active = false;
        bool lap_delta_valid = false;
        if (lap_timer_configured) {
            lap_timer_update(&lap_timer, &temp_gps, &lap_time, &lap_timing_active,
                             &best_lap_time, &lap_delta, &lap_delta_valid);
        }
        
        // Update global GPS data
        pthread_mutex_lock(&data_mutex);
        v_data.gps_data = temp_gps;
        v_data.gps_satellites = temp_gps.satellites_used;
        v_data.current_lap_time = lap_time;
        v_data.best_lap_time = best_lap_time;
        v_data.delta_time = lap_delta;
        v_data.lap_delta_valid = lap_delta_valid;
        v_data.lap_timing_active = lap_timing_active;
        pthread_mutex_unlock(&data_mutex);
        
        usleep(GPS_POLL_INTERVAL_US);
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
        IMUData temp_imu = {0};
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
        
        // Log first 50 samples for debugging
        if (log && sample_count < 50) {
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
        
        // Log first 50 samples for debugging
        if (log && sample_count < 50) {
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
    
    time_t next_mqtt_reconnect = 0;
    if (!mqtt_init(&config)) {
        if (log) {
            fprintf(log, "MQTT: Initial connect failed, retrying in 5 seconds...\n");
            fflush(log);
        }
        printf("MQTT: Initial connect failed, retrying in 5 seconds...\n");
        mqtt_cleanup();
        next_mqtt_reconnect = time(NULL) + 5;
    } else {
        printf("MQTT telemetry thread started\n");
    }
    
    // Publish telemetry at 25 Hz (every 40ms).
    int publish_count = 0;
    while(!shutdown_requested) {
        // Copy vehicle data with mutex protection
        VehicleData local_data;
        pthread_mutex_lock(&data_mutex);
        local_data = v_data;
        pthread_mutex_unlock(&data_mutex);

        // Testing mode: publish telemetry whenever MQTT is connected, including RPM 0.
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
        }

        if (!mqtt_is_connected() && time(NULL) >= next_mqtt_reconnect) {
            // Reconnect in the background so an offline broker never pauses CSV logging.
            if (log) {
                fprintf(log, "MQTT: Reconnecting...\n");
                fflush(log);
            }
            printf("MQTT: Reconnecting...\n");
            mqtt_cleanup();
            if (mqtt_init(&config)) {
                if (log) {
                    fprintf(log, "MQTT: Reconnected successfully\n");
                    fflush(log);
                }
            } else {
                mqtt_cleanup();
                next_mqtt_reconnect = time(NULL) + 5;
            }
        }
        // Publish at 25 Hz (40ms interval)
        usleep(40000);
    }
    
    if (log) fclose(log);
    mqtt_cleanup();
    return NULL;
}

// Local CSV logger thread. It is deliberately independent from MQTT so an
// unavailable broker or internet connection never pauses telemetry recording.
void *csv_thread(void *arg) {
    (void)arg;

    Config config;
    if (!config_load("/mnt/candata/config.txt", &config)) {
        printf("Failed to load config.txt, CSV logging disabled\n");
        return NULL;
    }

    if (!csv_logger_init("/mnt/candata/data")) {
        printf("Failed to initialize CSV logger\n");
        return NULL;
    }

    printf("CSV telemetry logger thread started\n");
    while (!shutdown_requested) {
        VehicleData local_data;
        pthread_mutex_lock(&data_mutex);
        local_data = v_data;
        pthread_mutex_unlock(&data_mutex);

        if (!csv_logger_append(&local_data, &config)) {
            printf("CSV: Failed to append telemetry\n");
        }

        usleep(40000);
    }

    csv_logger_close();
    return NULL;
}

int main(void)
{
    if (!install_signal_handlers()) {
        perror("Failed to install shutdown signal handlers");
        return 1;
    }

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
    pthread_t rx_th, tx_th, scx_th, ads_th, lambda_th, gps_th, imu_th, mag_th, csv_th, mqtt_th;
    
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

    // Start local CSV telemetry logging before MQTT; it continues while offline.
    pthread_create(&csv_th, NULL, csv_thread, NULL);
    pthread_setname_np(csv_th, "csv_log");
    printf("CSV telemetry logger thread started\n");
    
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

    // CAN1 is dedicated to the SCX Hub: receive buttons and send CAN0 RPM.
    if (access("/sys/class/net/can1", F_OK) == 0) {
        scx_can_init();
        pthread_create(&scx_th, NULL, scx_can_thread, NULL);
        pthread_setname_np(scx_th, "scx_can");
    } else {
        printf("SCX: can1 not found; SCX controls disabled\n");
    }

    printf("UI Loop Started on Core 0.\n");

    // 8. CAMERA SERVICE STATE (gst-stream@carN from config)
    // Read device number from config to build service name
    char cam_service[64] = "gst-stream@car0.service";
    {
        Config main_cfg;
        if (!config_load("/mnt/candata/config.txt", &main_cfg)) {
            main_cfg.device = 0;  // fallback: unknown
        }
        snprintf(cam_service, sizeof(cam_service),
                 "gst-stream@car%d.service", main_cfg.device);
    }
    printf("Camera service: %s\n", cam_service);
    // Stop at startup for clean state; loop will restart when engine on
    { char cmd[128]; snprintf(cmd, sizeof(cmd), "systemctl stop --no-block %s 2>/dev/null", cam_service); int _r = system(cmd); (void)_r; }
    bool camera_running = false;
    int cam_engine_off_ticks = 0;
    // 5 seconds of RPM==0 before stopping camera (312 ticks @ 16ms)
    const int CAM_STOP_TICKS = (5 * 1000000) / 16000;

    // 9. ENGINE-OFF SHUTDOWN DETECTION (commented out)
    // int engine_off_ticks = 0;
    // const int shutdown_ticks = (SHUTDOWN_DELAY_SEC * 1000000) / 16000;
    // bool shutdown_triggered = false;

    // 9. MAIN UI LOOP
    while(!shutdown_requested) {
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

        // Camera service: start on engine on, stop after 5s engine off
        {
            pthread_mutex_lock(&data_mutex);
            int cur_rpm = v_data.rpm;
            bool connected = v_data.can_connected;
            pthread_mutex_unlock(&data_mutex);

            if (connected && cur_rpm > 0) {
                cam_engine_off_ticks = 0;
                if (!camera_running) {
                    { char cmd[128]; snprintf(cmd, sizeof(cmd), "systemctl start --no-block %s", cam_service); int _r = system(cmd); (void)_r; }
                    camera_running = true;
                    printf("Engine ON — camera stream started\n");
                }
            } else {
                if (camera_running && ++cam_engine_off_ticks >= CAM_STOP_TICKS) {
                    { char cmd[128]; snprintf(cmd, sizeof(cmd), "systemctl stop --no-block %s", cam_service); int _r = system(cmd); (void)_r; }
                    camera_running = false;
                    cam_engine_off_ticks = 0;
                    printf("Engine OFF 5s — camera stream stopped\n");
                }
            }
        }

        // Advance LVGL internal time by 16ms
        lv_tick_inc(16);
        
        // Sleep to maintain ~60 FPS for smooth gauge animation
        usleep(16000);

        // Voluntary yield to ensure kernel scheduler runs smoothly
        sched_yield();
    }

    printf("Shutdown requested: flushing telemetry and stopping dashboard\n");
    fflush(stdout);

    if (camera_running) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "systemctl stop --no-block %s", cam_service);
        int result = system(cmd);
        (void)result;
    }

    pthread_join(csv_th, NULL);
    pthread_join(mqtt_th, NULL);
    printf("Dashboard shutdown complete\n");
    return 0;
}