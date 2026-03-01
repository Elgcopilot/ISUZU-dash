#define _GNU_SOURCE
#include "can_mgr.h"
#include "signals.h"
#include <stdio.h>

/* =========================================================
 * VEHICLE PROFILE — uncomment ONE line:
 * ========================================================= */
#define VEHICLE_PRIUS   // Toyota Prius 2013 test (RPM only via OBD)
//#define VEHICLE_ISUZU   // Isuzu D-Max full dashboard
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

static int s_rx = -1;
static int s_tx = -1;

void can_init() {
    printf("CAN: Resetting interface can0...\n");

    // Force down
    system("sudo ip link set can0 down");
    usleep(100000); // 100ms pause

    // Set bitrate and bring up with auto-restart
    system("sudo ip link set can0 up type can bitrate 500000 restart-ms 100");

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
            /* --- PRIUS: RPM + Battery Voltage via OBD --- */
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
                    }
                    break;
            }
#endif

            if (++sync_counter > 10) {
                pthread_mutex_lock(&data_mutex);
                v_data = local_data;
                v_data.can_connected = true;
                signals_update_lambda();
                signals_calculate_gear();
                pthread_mutex_unlock(&data_mutex);
                sync_counter = 0;

                // Sleep 1ms every 10 packets to prevent CPU starvation
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
    /* Prius test: RPM + Battery Voltage */
    static const uint8_t obd_pids[] = {
        0x0C, // Engine RPM
        0x42, // Control module voltage (12V battery)
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
        usleep(100000); // 100ms between polls
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
// ```

// ### **2. Important: Update `ui/ui.c`**
// Now that the simulator logic is in `can_mgr.c`, you **MUST remove** the temporary `#define DEMO_MODE` logic from your `ui.c` file, otherwise it will ignore the simulator thread and keep showing the fixed 1000 RPM you set earlier.

// In `ui/ui.c`, simply comment out or delete this line at the top:

// ```c
// // #define DEMO_MODE  <-- Comment this out!
// ```

// ### **3. How to Start the Simulator**
// By default, your `main.c` checks for `can0`.
// * **If CAN cable is connected:** It runs Real Mode (Reading from car).
// * **If CAN cable is disconnected/down:** It runs Simulator Mode.

// **To FORCE Simulator Mode (even if cable is connected):**
// Edit `app/main.c`:

// ```c
    // if(access("/sys/class/net/can0", F_OK) == 0) { ... }
    // else {
        // printf("Forcing Simulator...\n");
        // pthread_create(&rx_th, NULL, simulator_thread, NULL);
    // }
