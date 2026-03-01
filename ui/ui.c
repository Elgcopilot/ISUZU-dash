#include "ui.h"
#include "styles.h"
#include "signals.h"
#include "lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <math.h> 

// --- MODE SWITCH ---
// Comment this out to use real CANBUS/Vehicle data
//#define DEMO_MODE 

#ifndef PI
#define PI 3.14159265358979323846
#endif

// --- UI HANDLES ---
static lv_obj_t *lbl_voltage;
static lv_obj_t *batt_dot;     // Battery status indicator dot
static int blink_counter = 0;  // For blinking animation

// Gauge Handles (Linked to the BIG central numbers)
static lv_obj_t *arc_rpm, *lbl_rpm_val;
static lv_obj_t *arc_lambda, *lbl_lambda_val;
static lv_obj_t *arc_map, *lbl_map_val; 
static lv_obj_t *arc_duty, *lbl_duty_val;
static lv_obj_t *arc_rail, *lbl_rail_val;
static lv_obj_t *arc_clt, *lbl_clt_val;

// Demo Counter
#ifdef DEMO_MODE
static int demo_timer = 0;
#endif

// --- CONSTANTS ---
#define GAUGE_WIDTH  258  
#define GAUGE_HEIGHT 199  
#define GAUGE_GAP    6    
#define HEADER_H     50   
#define ARC_SIZE     200 
#define Y_OFFSET     25

// --- HELPER: Draw Standardized Cut Lines ---
static void create_gauge_cuts(lv_obj_t *parent) {
    float cx = (float)GAUGE_WIDTH / 2.0f; 
    float cy = ((float)GAUGE_HEIGHT / 2.0f) + (float)Y_OFFSET; 

    const float r_inner = 49.0f; 
    const float r_outer = 110.0f; 

    float angles[] = {202.5f, 270.0f, 337.5f};

    for(int i=0; i<3; i++) {
        float rad = angles[i] * (PI / 180.0f);
        lv_point_t * points = malloc(sizeof(lv_point_t) * 2);
        
        points[0].x = (lv_coord_t)(cx + r_inner * cos(rad));
        points[0].y = (lv_coord_t)(cy + r_inner * sin(rad));
        points[1].x = (lv_coord_t)(cx + r_outer * cos(rad));
        points[1].y = (lv_coord_t)(cy + r_outer * sin(rad));

        lv_obj_t * line = lv_line_create(parent);
        lv_line_set_points(line, points, 2);
        lv_obj_set_style_line_width(line, 5, 0);       
        lv_obj_set_style_line_color(line, lv_color_hex(0x000000), 0); 
        lv_obj_set_style_line_rounded(line, false, 0);   
        lv_obj_move_foreground(line);
    }
}

static void create_bold_gauge(lv_obj_t *parent, const char *title, int min, int max, 
                              const char *txt_l, const char *txt_m, const char *txt_r, 
                              int col, int row, lv_obj_t **arc_out, lv_obj_t **lbl_out) {
    
    int x_pos = GAUGE_GAP + (col * (GAUGE_WIDTH + GAUGE_GAP));
    int y_pos = HEADER_H + GAUGE_GAP + (row * (GAUGE_HEIGHT + GAUGE_GAP)); 

    // 1. CONTAINER
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, GAUGE_WIDTH, GAUGE_HEIGHT); 
    lv_obj_set_pos(cont, x_pos, y_pos); 
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(cont, 10, 0); 

    // 2. LAYER 1: BASE TRACK (Grey)
    lv_obj_t *static_bg = lv_arc_create(cont);
    lv_obj_set_size(static_bg, ARC_SIZE, ARC_SIZE);
    lv_arc_set_rotation(static_bg, 135);
    lv_arc_set_bg_angles(static_bg, 0, 270);
    lv_arc_set_value(static_bg, 0);
    lv_obj_align(static_bg, LV_ALIGN_CENTER, 0, Y_OFFSET);
    lv_obj_set_style_arc_color(static_bg, lv_color_hex(0x222222), LV_PART_MAIN); 
    lv_obj_set_style_arc_width(static_bg, 35, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(static_bg, false, LV_PART_MAIN);
    lv_obj_remove_style(static_bg, NULL, LV_PART_INDICATOR);
    lv_obj_remove_style(static_bg, NULL, LV_PART_KNOB);

    // 3. LAYER 2: WARNING ZONE (Red)
    lv_obj_t *red_zone = lv_arc_create(cont);
    lv_obj_set_size(red_zone, ARC_SIZE, ARC_SIZE);
    lv_arc_set_rotation(red_zone, 135);
    lv_arc_set_bg_angles(red_zone, 216, 270); 
    lv_arc_set_value(red_zone, 0); 
    lv_obj_align(red_zone, LV_ALIGN_CENTER, 0, Y_OFFSET);
    lv_obj_set_style_arc_color(red_zone, lv_color_hex(0x7B1113), LV_PART_MAIN); 
    lv_obj_set_style_arc_width(red_zone, 35, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(red_zone, false, LV_PART_MAIN);
    lv_obj_remove_style(red_zone, NULL, LV_PART_INDICATOR);
    lv_obj_remove_style(red_zone, NULL, LV_PART_KNOB);

    // 4. LAYER 3: MOVING VALUE (White Indicator)
    lv_obj_t *arc = lv_arc_create(cont);
    lv_obj_set_size(arc, ARC_SIZE, ARC_SIZE); 
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_value(arc, 0);
    lv_arc_set_range(arc, min, max);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, Y_OFFSET);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_MAIN); 
    lv_obj_set_style_arc_color(arc, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 35, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // 5. BLACK CUT LINES
    create_gauge_cuts(cont);

    // 6. CENTRAL VALUE (Large)
    lv_obj_t *lbl_val = lv_label_create(cont);
    lv_label_set_text(lbl_val, "0");
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_48, 0); 
    lv_obj_set_style_transform_zoom(lbl_val, 256, 0); 
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0xFFFFFF), 0); 
    lv_obj_align(lbl_val, LV_ALIGN_CENTER, 0, Y_OFFSET - 5); 
    lv_obj_move_foreground(lbl_val);

    // 7. TITLE & SCALE LABELS
    lv_obj_t *l_title = lv_label_create(cont);
    lv_label_set_text(l_title, title);
    lv_obj_set_style_text_font(l_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_zoom(l_title, 256, 0); // UPDATED ZOOM TO 256 (1x)
    lv_obj_set_style_text_color(l_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l_title, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_obj_t *l_min = lv_label_create(cont);
    lv_label_set_text(l_min, txt_l); 
    lv_obj_set_style_text_color(l_min, lv_color_hex(0x888888), 0);
    lv_obj_align(l_min, LV_ALIGN_BOTTOM_LEFT, 7, -118); 

    lv_obj_t *l_max = lv_label_create(cont);
    lv_label_set_text(l_max, txt_r); 
    lv_obj_set_style_text_color(l_max, lv_color_hex(0x888888), 0);
    lv_obj_align(l_max, LV_ALIGN_BOTTOM_RIGHT, -3, -118);

    lv_obj_t *l_mid = lv_label_create(cont);
    lv_label_set_text(l_mid, txt_m); 
    lv_obj_set_style_text_color(l_mid, lv_color_hex(0x888888), 0);
    lv_obj_align(l_mid, LV_ALIGN_TOP_MID, 0, Y_OFFSET + (-20)); 

    // 8. RETURN HANDLES
    *arc_out = arc;
    *lbl_out = lbl_val;
}

// --- HEADER ---
static void create_header(lv_obj_t *parent) {
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_size(header, 800, HEADER_H);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    // Voltage
    lv_obj_t *l_sys = lv_label_create(header);
    lv_label_set_text(l_sys, "SYSTEM");
    lv_obj_set_style_text_font(l_sys, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l_sys, lv_color_hex(0x888888), 0);
    lv_obj_align(l_sys, LV_ALIGN_TOP_LEFT, 10, 5);

    lbl_voltage = lv_label_create(header);
    lv_label_set_text(lbl_voltage, "--.-V");
    lv_obj_set_style_text_font(lbl_voltage, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_voltage, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_voltage, LV_ALIGN_BOTTOM_LEFT, 10, -5);
    
    // Battery Status Dot
    batt_dot = lv_obj_create(header);
    lv_obj_set_size(batt_dot, 6, 6);
    lv_obj_set_style_radius(batt_dot, 5, 0);
    lv_obj_set_style_bg_color(batt_dot, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_border_width(batt_dot, 0, 0);
    lv_obj_align(batt_dot, LV_ALIGN_LEFT_MID, 70, 0);

    // Page Numbers
    // 1 (Red)
    lv_obj_t *c1 = lv_obj_create(header);
    lv_obj_set_size(c1, 30, 30);
    lv_obj_set_style_radius(c1, 15, 0);
    lv_obj_set_style_bg_color(c1, lv_color_hex(0xD32F2F), 0); 
    lv_obj_set_style_border_width(c1, 0, 0);
    lv_obj_align(c1, LV_ALIGN_CENTER, -40, 0);
    lv_obj_clear_flag(c1, LV_OBJ_FLAG_SCROLLABLE); 
    lv_obj_set_style_pad_all(c1, 0, 0); 

    lv_obj_t *l1 = lv_label_create(c1);
    lv_label_set_text(l1, "1");
    lv_obj_set_style_text_color(l1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l1);

    // 2 (Black)
    lv_obj_t *c2 = lv_obj_create(header);
    lv_obj_set_size(c2, 30, 30);
    lv_obj_set_style_radius(c2, 15, 0);
    lv_obj_set_style_bg_color(c2, lv_color_hex(0x000000), 0); 
    lv_obj_set_style_border_width(c2, 1, 0);
    lv_obj_set_style_border_color(c2, lv_color_hex(0x555555), 0);
    lv_obj_align(c2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(c2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(c2, 0, 0);

    lv_obj_t *l2 = lv_label_create(c2);
    lv_label_set_text(l2, "2");
    lv_obj_set_style_text_color(l2, lv_color_hex(0x888888), 0);
    lv_obj_center(l2);

    // 3 (Black)
    lv_obj_t *c3 = lv_obj_create(header);
    lv_obj_set_size(c3, 30, 30);
    lv_obj_set_style_radius(c3, 15, 0);
    lv_obj_set_style_bg_color(c3, lv_color_hex(0x000000), 0); 
    lv_obj_set_style_border_width(c3, 1, 0);
    lv_obj_set_style_border_color(c3, lv_color_hex(0x555555), 0);
    lv_obj_align(c3, LV_ALIGN_CENTER, 40, 0);
    lv_obj_clear_flag(c3, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(c3, 0, 0);

    lv_obj_t *l3 = lv_label_create(c3);
    lv_label_set_text(l3, "3");
    lv_obj_set_style_text_color(l3, lv_color_hex(0x888888), 0);
    lv_obj_center(l3);

    // Settings
    lv_obj_t *l_perf = lv_label_create(header);
    lv_label_set_text(l_perf, "PERFORMANCE");
    lv_obj_set_style_text_font(l_perf, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l_perf, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(l_perf, LV_ALIGN_TOP_RIGHT, -10, 5);

    lv_obj_t *l_sub = lv_label_create(header);
    lv_label_set_text(l_sub, "RACE MODE ACTIVE");
    lv_obj_set_style_text_font(l_sub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l_sub, lv_color_hex(0x555555), 0);
    lv_obj_align(l_sub, LV_ALIGN_BOTTOM_RIGHT, -10, -5);
}

void ui_init() {
    styles_init();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    // Main Layout
    lv_obj_t *layout = lv_obj_create(scr);
    lv_obj_set_size(layout, 800, 480);
    lv_obj_set_style_pad_all(layout, 0, 0);
    lv_obj_set_style_bg_color(layout, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(layout, 0, 0);
    
    create_header(layout);

    // Row 0
    create_bold_gauge(layout, "ENGINE RPM", 0, 6000, "1500", "3000", "4500", 0, 0, &arc_rpm, &lbl_rpm_val);
    create_bold_gauge(layout, "LAMBDA", 0, 100, "", "1.00", "", 1, 0, &arc_lambda, &lbl_lambda_val);
    create_bold_gauge(layout, "MAP PSI", 0, 30, "0", "15", "30", 2, 0, &arc_map, &lbl_map_val);

    // Row 1
    create_bold_gauge(layout, "DUTY INJ %", 0, 100, "0", "50", "100", 0, 1, &arc_duty, &lbl_duty_val);
    create_bold_gauge(layout, "RAIL PRES", 0, 200, "0", "100", "200", 1, 1, &arc_rail, &lbl_rail_val);
    create_bold_gauge(layout, "COOLANT C", 0, 120, "0", "60", "120", 2, 1, &arc_clt, &lbl_clt_val);
}

void ui_update() {
    VehicleData d;

#ifdef DEMO_MODE
    // --- FORCE VALUES FOR TESTING ---
    static int demo_timer = 0;
    demo_timer += 50; 
    float wave = (sinf(demo_timer * 0.001f) + 1.0f) / 2.0f;
    d.rpm = 1000 + (int)(wave * 5000); 
    d.lambda = 0.50f + (wave * 1.0f);
    d.boost = (int)(wave * 30.0f /10);
    d.duty_injection = wave * 100.0f;
    d.fuel_rail_press = wave * 200.0f;
    d.coolant_temp = (int)(40 + (wave * 80));
    d.speed_obd = (int)(wave * 260);
    d.gear = (int)(wave * 6); if(d.gear==0) d.gear=1;
    d.pedal_pos = (int)(wave * 100);
    d.brake_pos = (int)((1.0f-wave) * 100);
#else
    if (pthread_mutex_trylock(&data_mutex) == 0) {
        d = v_data; 
        pthread_mutex_unlock(&data_mutex);
    } else { return; }
#endif

    // RPM
    if (arc_rpm) lv_arc_set_value(arc_rpm, d.rpm);
    if (lbl_rpm_val) {
        lv_label_set_text_fmt(lbl_rpm_val, "%d", d.rpm);
        if (d.rpm > 5000) lv_obj_set_style_text_color(lbl_rpm_val, lv_color_hex(0xD32F2F), 0);
        else lv_obj_set_style_text_color(lbl_rpm_val, lv_color_hex(0xFFFFFF), 0);
    }

    // LAMBDA FIX
    int lambda_scaled = (int)((d.lambda - 0.50f) * 100);
    if (lambda_scaled < 0) lambda_scaled = 0;
    if (lambda_scaled > 100) lambda_scaled = 100;
    
    if (arc_lambda) lv_arc_set_value(arc_lambda, lambda_scaled);
    
    if (lbl_lambda_val) {
        int whole = (int)d.lambda;
        int dec = (int)((d.lambda - whole) * 100);
        lv_label_set_text_fmt(lbl_lambda_val, "%d.%02d", whole, abs(dec));
    }

    // BOOST
    int boost_psi = (int)(d.boost * 14.5f); 
    if (arc_map) lv_arc_set_value(arc_map, boost_psi);
    if (lbl_map_val) lv_label_set_text_fmt(lbl_map_val, "%d", boost_psi);

    // DUTY
    if (arc_duty) lv_arc_set_value(arc_duty, (int)d.duty_injection);
    if (lbl_duty_val) {
        int w = (int)d.duty_injection;
        int dec = (int)((d.duty_injection - w) * 10);
        lv_label_set_text_fmt(lbl_duty_val, "%d.%d", w, abs(dec));
    }

    // RAIL
    if (arc_rail) lv_arc_set_value(arc_rail, (int)d.fuel_rail_press);
    if (lbl_rail_val) lv_label_set_text_fmt(lbl_rail_val, "%d", (int)d.fuel_rail_press);

    // COOLANT
    if (arc_clt) lv_arc_set_value(arc_clt, d.coolant_temp);
    if (lbl_clt_val) lv_label_set_text_fmt(lbl_clt_val, "%d", d.coolant_temp);

    // BATTERY VOLTAGE (LVGL doesn't support %f, use integer math)
    if (lbl_voltage) {
        if (d.battery_voltage > 0.1f) {
            int v_whole = (int)d.battery_voltage;
            int v_dec = (int)((d.battery_voltage - v_whole) * 10);
            if (v_dec < 0) v_dec = -v_dec;
            lv_label_set_text_fmt(lbl_voltage, "%d.%dV", v_whole, v_dec);
        } else {
            lv_label_set_text(lbl_voltage, "--.-V");
        }
    }

    // BATTERY STATUS DOT
    if (batt_dot) {
        if (d.battery_voltage < 0.1f) {
            // No data yet — grey
            lv_obj_set_style_bg_color(batt_dot, lv_color_hex(0x555555), 0);
            lv_obj_set_style_bg_opa(batt_dot, LV_OPA_COVER, 0);
        } else if (d.battery_voltage < 11.8f) {
            // LOW BATTERY — red blinking
            blink_counter++;
            if ((blink_counter / 8) % 2 == 0) {
                lv_obj_set_style_bg_color(batt_dot, lv_color_hex(0xFF0000), 0);
                lv_obj_set_style_bg_opa(batt_dot, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_opa(batt_dot, LV_OPA_TRANSP, 0);
            }
            // Also turn voltage text red
            if (lbl_voltage)
                lv_obj_set_style_text_color(lbl_voltage, lv_color_hex(0xFF0000), 0);
        } else {
            // Normal — green
            blink_counter = 0;
            lv_obj_set_style_bg_color(batt_dot, lv_color_hex(0x00FF00), 0);
            lv_obj_set_style_bg_opa(batt_dot, LV_OPA_COVER, 0);
            if (lbl_voltage)
                lv_obj_set_style_text_color(lbl_voltage, lv_color_hex(0xFFFFFF), 0);
        }
    }
}