#define _GNU_SOURCE
#include "can_mgr.h"
#include "signals.h"
#include "ui.h"
#include <stdio.h>

/* =========================================================
 * VEHICLE PROFILE — uncomment ONE line:
 * ========================================================= */
#define VEHICLE_ISUZU   // Isuzu D-Max full dashboard
//#define VEHICLE_PRIUS   // Toyota Prius 2013 test (RPM only via OBD)
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <math.h> // For sine wave simulation
#include <time.h>

static int s_rx = -1;
static int s_tx = -1;
static int s_scx = -1;

#define SCX_FUNCTION_STATE_ID 0x121
#define SCX_RPM_REPORT_ID     0x212
#define SCX_RPM_PERIOD_MS     40
#define SCX_HEARTBEAT_MS      3000

static long long monotonic_milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000LL + now.tv_nsec / 1000000LL;
}

static void scx_process_pressed_functions(uint32_t pressed_functions) {
    if (pressed_functions & (1U << 0)) ui_request_navigation(UI_NAV_UP);     // Function 1
    if (pressed_functions & (1U << 1)) ui_request_navigation(UI_NAV_LEFT);   // Function 2
    if (pressed_functions & (1U << 2)) ui_request_navigation(UI_NAV_DOWN);   // Function 3
    if (pressed_functions & (1U << 3)) ui_request_navigation(UI_NAV_RIGHT);  // Function 4
    if (pressed_functions & (1U << 4)) ui_request_navigation(UI_NAV_ENTER);  // Function 5
}

void scx_can_init() {
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_filter filter = {
        .can_id = SCX_FUNCTION_STATE_ID,
        .can_mask = CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG,
    };

    printf("SCX: Configuring interface can1 at 500 kbit/s...\n");
    int command_result = system("sudo ip link set can1 down");
    (void)command_result;
    command_result = system("sudo ip link set can1 up type can bitrate 500000 sample-point 0.625 restart-ms 100");
    (void)command_result;

    if ((s_scx = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("SCX Socket");
        return;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "can1", IFNAMSIZ - 1);
    if (ioctl(s_scx, SIOCGIFINDEX, &ifr) < 0) {
        perror("SCX Interface");
        close(s_scx);
        s_scx = -1;
        return;
    }

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    fcntl(s_scx, F_SETFL, O_NONBLOCK);
    setsockopt(s_scx, SOL_CAN_RAW, CAN_RAW_FILTER, &filter, sizeof(filter));

    int recv_own = 0;
    setsockopt(s_scx, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own));
    if (bind(s_scx, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("SCX Bind");
        close(s_scx);
        s_scx = -1;
        return;
    }

    printf("SCX: CAN1 receiver and RPM sender ready\n");
}

void *scx_can_thread(void *arg) {
    (void)arg;

    uint32_t function_state = 0;
    long long last_heartbeat = monotonic_milliseconds();
    long long next_rpm_send = last_heartbeat;
    bool heartbeat_missing_reported = false;
    bool rpm_transmit_failed = false;

    while (1) {
        if (s_scx < 0) {
            sleep(1);
            continue;
        }

        struct can_frame frame;
        while (read(s_scx, &frame, sizeof(frame)) == sizeof(frame)) {
            if (frame.can_id != SCX_FUNCTION_STATE_ID || frame.can_dlc != 8 ||
                frame.data[0] != 0 || frame.data[4] & 0xC0 ||
                frame.data[5] != 0 || frame.data[6] != 0 || frame.data[7] != 0) {
                continue;
            }

            uint32_t next_state = (uint32_t)frame.data[1] |
                                  ((uint32_t)frame.data[2] << 8) |
                                  ((uint32_t)frame.data[3] << 16) |
                                  ((uint32_t)(frame.data[4] & 0x3F) << 24);
            scx_process_pressed_functions(next_state & ~function_state);
            function_state = next_state;
            last_heartbeat = monotonic_milliseconds();
            heartbeat_missing_reported = false;
        }

        long long now = monotonic_milliseconds();
        if (now >= next_rpm_send) {
            int rpm;
            pthread_mutex_lock(&data_mutex);
            rpm = v_data.rpm;
            pthread_mutex_unlock(&data_mutex);

            if (rpm < 0) rpm = 0;
            if (rpm > 65535) rpm = 65535;

            memset(&frame, 0, sizeof(frame));
            frame.can_id = SCX_RPM_REPORT_ID;
            frame.can_dlc = 8;
            frame.data[6] = (uint8_t)(rpm >> 8);
            frame.data[7] = (uint8_t)rpm;

            if (write(s_scx, &frame, sizeof(frame)) < 0) {
                if (!rpm_transmit_failed && errno != EAGAIN && errno != ENOBUFS) {
                    perror("SCX RPM transmit");
                }
                rpm_transmit_failed = true;
            } else {
                if (rpm_transmit_failed) printf("SCX: RPM transmission restored\n");
                rpm_transmit_failed = false;
            }
            next_rpm_send = now + SCX_RPM_PERIOD_MS;
        }

        if (!heartbeat_missing_reported && now - last_heartbeat >= SCX_HEARTBEAT_MS) {
            printf("SCX: Function-state heartbeat missing; clearing button states\n");
            function_state = 0;
            heartbeat_missing_reported = true;
        }

        usleep(5000);
    }

    return NULL;
}

void can_init() {
    printf("CAN: Resetting interface can0...\n");

    // Force down
    int command_result = system("sudo ip link set can0 down");
    (void)command_result;
    usleep(100000); // 100ms pause

    // Set bitrate and bring up with auto-restart
    command_result = system("sudo ip link set can0 up type can bitrate 500000 restart-ms 100");
    (void)command_result;

    // Give the kernel 1 second to initialize the driver
    sleep(1);
    printf("CAN: Interface can0 is up.\n");

    struct sockaddr_can addr;
    struct ifreq ifr;

    // --- RX SOCKET ---
    if ((s_rx = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) { perror("RX Socket"); return; }
    strcpy(ifr.ifr_name, "can0");
    ioctl(s_rx, SIOCGIFINDEX, &ifr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    fcntl(s_rx, F_SETFL, O_NONBLOCK);
    int rcvbuf = 1024 * 1024; 
    setsockopt(s_rx, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    if (bind(s_rx, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("RX Bind"); return; }

    // Don't receive our own transmitted frames
    int recv_own = 0;
    setsockopt(s_rx, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &recv_own, sizeof(recv_own));

    // --- TX SOCKET ---
    if ((s_tx = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) { perror("TX Socket"); return; }
    fcntl(s_tx, F_SETFL, O_NONBLOCK);
    if (bind(s_tx, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("TX Bind"); return; }

    printf("CAN Init: DUAL SOCKET MODE ACTIVE\n");
}

void *can_rx_thread(void *arg) {
    // Pin to CPU Core 1
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(1, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    struct can_frame frame;
    VehicleData local_data; 
    memset(&local_data, 0, sizeof(VehicleData));
    int sync_counter = 0;

    while (1) {
        if (read(s_rx, &frame, sizeof(struct can_frame)) > 0) {
            uint32_t id = frame.can_id & CAN_SFF_MASK;
            
#ifdef VEHICLE_PRIUS
            /* --- PRIUS: RPM + Battery Voltage + Coolant via OBD --- */
            if (id == 0x7E8 && frame.data[1] == 0x41) {
                int pid = frame.data[2];
                int A = frame.data[3];
                int B = frame.data[4];
                if (pid == 0x0C) {
                    local_data.rpm = (A * 256 + B) / 4;
                    printf("RPM: %d\n", local_data.rpm);
                }
                if (pid == 0x42) {
                    local_data.battery_voltage = (A * 256 + B) / 1000.0f;
                    printf("Battery: %.1fV\n", local_data.battery_voltage);
                }
                if (pid == 0x05) {
                    local_data.coolant_temp = A - 40;
                    printf("Coolant: %d C\n", local_data.coolant_temp);
                }
            }
#endif

#ifdef VEHICLE_ISUZU
            /* --- ISUZU D-MAX: Full decoder --- */
            switch (id) {
                case 0x160: local_data.rpm = ((frame.data[0] << 8) | frame.data[1]) / 8; break;
                case 0x151: local_data.duty_injection = ((frame.data[0] << 8) | frame.data[1]) / 100.0f; break;
                case 0x162: // Wheel speeds
                    local_data.speed_fl = ((frame.data[0] << 8) | frame.data[1]) / 256.0f;
                    local_data.speed_fr = ((frame.data[2] << 8) | frame.data[3]) / 256.0f;
                    local_data.speed_rl = ((frame.data[4] << 8) | frame.data[5]) / 256.0f;
                    local_data.speed_rr = ((frame.data[6] << 8) | frame.data[7]) / 256.0f;
                    local_data.speed_avg = (local_data.speed_fl + local_data.speed_fr + local_data.speed_rl + local_data.speed_rr) / 4.0f;
                    break;
                case 0x150: local_data.pedal_pos = (frame.data[6] * 100) / 250; break;
                case 0x166:
                    { int inv = 145 - frame.data[1];
                      if(inv < 0) inv = 0; if(inv > 100) inv = 100;
                      local_data.brake_pos = inv; } break;
                case 0x7E8: // OBD Reply
                    if (frame.data[1] == 0x41) {
                        int pid = frame.data[2];
                        int A = frame.data[3];
                        int B = frame.data[4];
                        if (pid == 0x05) local_data.coolant_temp = A - 40;
                        if (pid == 0x5C) local_data.oil_temp = A - 40;
                        if (pid == 0x0B) local_data.boost = (A > 100) ? (A - 100) : 0;
                        if (pid == 0x0D) local_data.speed_obd = A;
                        if (pid == 0x23) local_data.fuel_rail_press = ((A * 256 + B) * 10) / 1000.0f;
                        if (pid == 0x10) local_data.maf = (A * 256 + B) / 100.0f;
                        if (pid == 0x5E) local_data.fuel_rate = (A * 256 + B) / 20.0f;
                        if (pid == 0x0F) local_data.intake_temp = A - 40;
                        if (pid == 0x42) local_data.battery_voltage = (A * 256 + B) / 1000.0f;
                    }
                    break;
            }
#endif

            if (++sync_counter > 3) {
                pthread_mutex_lock(&data_mutex);
                // Copy only CAN-derived fields — do NOT overwrite sensor/GPS fields
                v_data.rpm               = local_data.rpm;
                v_data.duty_injection    = local_data.duty_injection;
                v_data.speed_fl          = local_data.speed_fl;
                v_data.speed_fr          = local_data.speed_fr;
                v_data.speed_rl          = local_data.speed_rl;
                v_data.speed_rr          = local_data.speed_rr;
                v_data.speed_avg         = local_data.speed_avg;
                v_data.pedal_pos         = local_data.pedal_pos;
                v_data.brake_pos         = local_data.brake_pos;
                v_data.coolant_temp      = local_data.coolant_temp;
                v_data.oil_temp          = local_data.oil_temp;
                // boost is written by ADS1115 thread (AIN1 physical sensor)
                v_data.speed_obd         = local_data.speed_obd;
                v_data.fuel_rail_press   = local_data.fuel_rail_press;
                v_data.maf               = local_data.maf;
                v_data.fuel_rate         = local_data.fuel_rate;
                v_data.intake_temp       = local_data.intake_temp;
                v_data.battery_voltage   = local_data.battery_voltage;
                v_data.can_connected     = true;
                // lambda is written by lambda_thread (I2C 0x33) — do not recalculate here
                signals_calculate_gear();
                pthread_mutex_unlock(&data_mutex);
                sync_counter = 0;

                // Sleep 1ms every few packets to prevent CPU starvation
                usleep(1000);
            }
        }
        usleep(100); 
    }
    return NULL;
}

void *can_tx_obd_thread(void *arg) {
    cpu_set_t cpuset; CPU_ZERO(&cpuset); CPU_SET(2, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    printf("TX Thread: OBD Polling ENABLED (0x7DF)\n");

#ifdef VEHICLE_PRIUS
    /* Prius test: RPM + Battery Voltage + Coolant */
    static const uint8_t obd_pids[] = {
        0x0C, // Engine RPM
        0x42, // Control module voltage (12V battery)
        0x05, // Coolant temperature
    };
#endif

#ifdef VEHICLE_ISUZU
    /* Isuzu: full PID list */
    static const uint8_t obd_pids[] = {
        0x05, // Coolant temperature
        0x5C, // Oil temperature
        0x0B, // MAP / boost pressure
        0x0D, // Vehicle speed
        0x23, // Fuel rail pressure
        0x0F, // Intake air temperature
        0x10, // MAF air flow rate
        0x5E, // Engine fuel rate
        0x42, // Control module voltage (12V battery)
    };
#endif

    const int num_pids = sizeof(obd_pids) / sizeof(obd_pids[0]);
    int i = 0;

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id = 0x7DF;
    frame.can_dlc = 8;

    while (1) {
        if (s_tx < 0) { sleep(1); continue; }
        memset(frame.data, 0, 8);
        frame.data[0] = 0x02;
        frame.data[1] = 0x01;
        frame.data[2] = obd_pids[i];
        if (write(s_tx, &frame, sizeof(struct can_frame)) > 0) {
            i = (i + 1) % num_pids;
        }
        usleep(50000); // 50ms between polls — faster data for smooth gauges
    }
    return NULL;
}

// --- SIMULATOR THREAD ---
// Generates fake sine wave data for testing
void *simulator_thread(void *arg) {
    printf("--- SIMULATOR MODE RUNNING ---\n");
    //float t = 0.0f;

    while(1) sleep(1);
    return NULL;
    //    pthread_mutex_lock(&data_mutex);
        
        // Create a 0.0 to 1.0 oscillating wave
    //    float wave = (sinf(t) + 1.0f) / 2.0f; 

        // Map wave to sensor ranges
    //    v_data.rpm = (int)(wave * 6000);
    //    v_data.speed_obd = (int)(wave * 240);
    //   v_data.boost = (int)(wave * 30); // 0-30 PSI
    //    v_data.lambda = 0.7f + (wave * 0.6f); // 0.7 - 1.3
    //    v_data.duty_injection = wave * 100.0f;
    //    v_data.fuel_rail_press = wave * 180.0f;
    //    v_data.coolant_temp = (int)(60 + (wave * 50));
    //    v_data.oil_temp = (int)(70 + (wave * 40));
    //    v_data.oil_pressure = wave * 5.0f;
    //    v_data.pedal_pos = (int)(wave * 100);
    //    v_data.brake_pos = (int)((1.0f - wave) * 100); // Inverse of pedal
    //    v_data.gear = (int)(wave * 6);
    //    if (v_data.gear == 0) v_data.gear = 1;

     //   v_data.can_connected = false; // Mark as not real CAN
        
    //    pthread_mutex_unlock(&data_mutex);

    //    t += 0.05f;    // Speed of animation
    //    usleep(33000); // 30 FPS update
   // }
   // return NULL;
}
