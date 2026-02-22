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

#define DISP_BUF_SIZE (800 * 480 / 10)

int main(void)
{
    lv_init();
    fbdev_init();

    static lv_color_t buf[DISP_BUF_SIZE];
    static lv_disp_draw_buf_t disp_buf;
    lv_disp_draw_buf_init(&disp_buf, buf, NULL, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    disp_drv.hor_res = 800;
    disp_drv.ver_res = 480;
    lv_disp_drv_register(&disp_drv);

    signals_init();
    ui_init();

    pthread_t rx_th, tx_th;
    
    if(access("/sys/class/net/can0", F_OK) == 0) {
        printf("Starting Real Mode with HEARTBEAT...\n");
        can_init();
        pthread_create(&rx_th, NULL, can_rx_thread, NULL);
        pthread_create(&tx_th, NULL, can_tx_obd_thread, NULL);
    } else {
        printf("Starting Simulator Mode...\n");
        pthread_create(&rx_th, NULL, simulator_thread, NULL);
    }

    int heartbeat = 0;

    while(1) {
        lv_timer_handler();
        ui_update();
        
        lv_tick_inc(33);
        usleep(33000);

        // --- HEARTBEAT ---
        // Prints a dot every ~1 second.
        // If dots stop, the CPU is frozen.
        heartbeat++;
        if (heartbeat >= 30) { 
            printf("."); 
            fflush(stdout); 
            heartbeat = 0;
        }
    }

    return 0;
}