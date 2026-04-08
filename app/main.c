#define _GNU_SOURCE
#include "lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/indev/evdev.h"
#include "ui.h"
#include "styles.h"
#include "can_mgr.h"
#include "signals.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <sched.h>
#include <stdlib.h>
#include "app_config.h"

#define DISP_BUF_SIZE (800 * 480 / 10)

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
    pthread_t rx_th, tx_th;
    
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