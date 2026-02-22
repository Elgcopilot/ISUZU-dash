#include "can_mgr.h"
#include "signals.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <errno.h>

static int s = -1;

void can_init() {
    printf("CAN: Resetting interface can0...\n");
    
    // Force down
    system("sudo ip link set can0 down");
    usleep(100000); // 100ms pause

    // Set bitrate and bring up
    // We add 'restart-ms 100' so the driver auto-recovers if the bus crashes
    system("sudo ip link set can0 up type can bitrate 500000 restart-ms 100");
    
    // Give the kernel 1 second to initialize the driver before we open sockets
    sleep(1); 
    printf("CAN: Interface can0 is up.\n");

    struct sockaddr_can addr;
    struct ifreq ifr;
    
    if ((s = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
        perror("Socket Error"); return;
    }

    strcpy(ifr.ifr_name, "can0");
    ioctl(s, SIOCGIFINDEX, &ifr);

    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    
    // Safety: Increase Buffer to 1MB
    int rcvbuf_size = 1024 * 1024; 
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf_size, sizeof(rcvbuf_size));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Bind Error"); return;
    }
    
    // Safety: 100ms Timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000; 
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    printf("CAN Init: PASSIVE MODE ACTIVE (No TX)\n");
}

void *can_rx_thread(void *arg) {
    struct can_frame frame;
    VehicleData local_data; 
    memset(&local_data, 0, sizeof(VehicleData));
    int sync_counter = 0;

    printf("RX Thread: Started.\n");

    while (1) {
        if (s < 0) { sleep(1); continue; }
        
        int nbytes = read(s, &frame, sizeof(struct can_frame));

        if (nbytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue; 
            usleep(50000); // 50ms Error Backoff
            continue; 
        }

        uint32_t id = frame.can_id & CAN_SFF_MASK;

        switch (id) {
            case 0x160: local_data.rpm = ((frame.data[0] << 8) | frame.data[1]) / 8; break;
            case 0x151: local_data.duty_injection = ((frame.data[0] << 8) | frame.data[1]) / 100.0f; break;
            case 0x162: 
                local_data.speed_fl = ((frame.data[0] << 8) | frame.data[1]) / 256.0f;
                local_data.speed_fr = ((frame.data[2] << 8) | frame.data[3]) / 256.0f;
                local_data.speed_rl = ((frame.data[4] << 8) | frame.data[5]) / 256.0f;
                local_data.speed_rr = ((frame.data[6] << 8) | frame.data[7]) / 256.0f;
                local_data.speed_avg = (local_data.speed_fl + local_data.speed_fr + local_data.speed_rl + local_data.speed_rr) / 4.0f;
                break;
            case 0x150: local_data.pedal_pos = (frame.data[6] * 100) / 255; break;
            case 0x166: 
                { int inv = 145 - frame.data[1]; 
                  if(inv < 0) inv = 0; if(inv > 100) inv = 100; 
                  local_data.brake_pos = inv; } break;
            case 0x4A1:
                { float map_kpa = ((frame.data[0] - 30) * 1.14f) + 101.0f;
                  if (map_kpa < 101.0f) map_kpa = 101.0f;
                  local_data.boost = (int)(map_kpa + 0.5f); }
                break;
            case 0x4A2:
                local_data.coolant_temp = frame.data[2] - 40;
                local_data.oil_temp = frame.data[5] - 40;
                break;
            case 0x7E8: 
                if (frame.data[1] == 0x41) {
                    int pid = frame.data[2]; int A = frame.data[3]; int B = frame.data[4];
                    if (pid == 0x0D) local_data.speed_obd = A;
                    if (pid == 0x23) local_data.fuel_rail_press = ((A * 256 + B) * 10) / 1000.0f;
                    if (pid == 0x10) local_data.maf = (A * 256 + B) / 100.0f;
                    if (pid == 0x5E) local_data.fuel_rate = (A * 256 + B) / 20.0f;
                    if (pid == 0x0F) local_data.intake_temp = A - 40;
                }
                break;
        }
        
        // Batch Update
        if (++sync_counter > 10) {
            pthread_mutex_lock(&data_mutex);
            v_data = local_data; 
            v_data.can_connected = true;
            signals_update_lambda();
            signals_calculate_gear();
            pthread_mutex_unlock(&data_mutex);
            sync_counter = 0;
            
            // --- INCREASED SLEEP FOR STABILITY ---
            // Sleeps 1ms (1000us) every 10 packets.
            // This prevents CPU Starvation.
            usleep(1000); 
        }
    }
    return NULL;
}

void *can_tx_obd_thread(void *arg) {
    printf("TX Thread: OBD Polling ENABLED (0x7DF), polling PIDs in round-robin order\n");
    // Format: [0x02, 0x01, PID, 0x00 x5]
    static const uint8_t obd_pids[] = {
        0x0D, // Vehicle speed
        0x23, // Fuel rail pressure (gauge) -> rail_press
        0x0F, // Intake air temperature
        0x10, // MAF air flow rate
        0x5E, // Engine fuel rate
    };
    const int num_pids = sizeof(obd_pids) / sizeof(obd_pids[0]);
    int pid_idx = 0;

    while (1) {
        if (s < 0) { sleep(1); continue; }

        struct can_frame req;
        memset(&req, 0, sizeof(req));
        req.can_id  = 0x7DF;       // OBD-II functional broadcast address
        req.can_dlc = 8;
        req.data[0] = 0x02;        // Number of additional data bytes
        req.data[1] = 0x01;        // Service 01 – current data
        req.data[2] = obd_pids[pid_idx];

        if (write(s, &req, sizeof(req)) < 0) {
            // Silently retry; ECU may be temporarily unavailable
        }

        pid_idx = (pid_idx + 1) % num_pids;

        // 100 ms between polls – gives ECU time to respond before next request
        usleep(100000);
    }
    return NULL;
}

void *simulator_thread(void *arg) { return NULL; }