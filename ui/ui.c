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

// --- SMOOTHING ---
// Exponential moving average alpha (0.0=frozen, 1.0=instant)
// 0.15 at 60 FPS gives a nice ~100ms settling feel
#define SMOOTH_ALPHA 0.15f

typedef struct {
    float rpm;
    float speed;
    float oil_temp;
    float lambda;
    float boost;
    float duty;
    float rail;
    float coolant;
    float battery_voltage;
} SmoothedData;

static SmoothedData sm = {0};  // current smoothed display values

static float smooth_lerp(float current, float target, float alpha) {
    float diff = target - current;
    // If close enough, snap to avoid endless tiny updates
    if (diff > -0.5f && diff < 0.5f) return target;
    return current + diff * alpha;
}

// --- UI HANDLES ---
static lv_obj_t *lbl_voltage;
static lv_obj_t *batt_dot;     // Battery status indicator dot
static int blink_counter = 0;  // For blinking animation

// Page system
#define NUM_PAGES 3
#define PAGE_CYCLE_SEC 5
static int current_page = 0;
static int page_timer = 0;
static lv_obj_t *page_cont[NUM_PAGES];       // Page containers
static lv_obj_t *page_dot[NUM_PAGES];         // Page indicator circles
static lv_obj_t *page_dot_lbl[NUM_PAGES];     // Labels inside circles

// Page 1 gauges: RPM, Lambda, MAP, Duty, Rail, Coolant
static lv_obj_t *arc_rpm, *lbl_rpm_val;
static lv_obj_t *p1_arc_lambda, *p1_lbl_lambda;
static lv_obj_t *p1_arc_map, *p1_lbl_map;
static lv_obj_t *p1_arc_duty, *p1_lbl_duty;
static lv_obj_t *p1_arc_rail, *p1_lbl_rail;
static lv_obj_t *p1_arc_clt, *p1_lbl_clt;

// Page 2 gauges: Speed, Lambda, MAP, Duty, Rail, Coolant
static lv_obj_t *arc_speed, *lbl_speed_val;
static lv_obj_t *p2_arc_lambda, *p2_lbl_lambda;
static lv_obj_t *p2_arc_map, *p2_lbl_map;
static lv_obj_t *p2_arc_duty, *p2_lbl_duty;
static lv_obj_t *p2_arc_rail, *p2_lbl_rail;
static lv_obj_t *p2_arc_clt, *p2_lbl_clt;

// Page 3 gauges: Oil Temp, Lambda, MAP, Duty, Rail, Coolant
static lv_obj_t *arc_oil, *lbl_oil_val;
static lv_obj_t *p3_arc_lambda, *p3_lbl_lambda;
static lv_obj_t *p3_arc_map, *p3_lbl_map;
static lv_obj_t *p3_arc_duty, *p3_lbl_duty;
static lv_obj_t *p3_arc_rail, *p3_lbl_rail;
static lv_obj_t *p3_arc_clt, *p3_lbl_clt;

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
    int y_pos = GAUGE_GAP + (row * (GAUGE_HEIGHT + GAUGE_GAP)); 

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
    lv_obj_set_style_text_font(l_min, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l_min, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(l_min, LV_ALIGN_BOTTOM_LEFT, 5, -116); 

    lv_obj_t *l_max = lv_label_create(cont);
    lv_label_set_text(l_max, txt_r); 
    lv_obj_set_style_text_font(l_max, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l_max, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(l_max, LV_ALIGN_BOTTOM_RIGHT, -1, -116);

    lv_obj_t *l_mid = lv_label_create(cont);
    lv_label_set_text(l_mid, txt_m); 
    lv_obj_set_style_text_font(l_mid, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l_mid, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(l_mid, LV_ALIGN_TOP_MID, 0, Y_OFFSET + (-22)); 

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

    // Page Number Indicators — styled like reference image
    const int page_x[] = {-50, 0, 50};
    for (int i = 0; i < NUM_PAGES; i++) {
        page_dot[i] = lv_obj_create(header);
        lv_obj_set_size(page_dot[i], 38, 38);
        lv_obj_set_style_radius(page_dot[i], 19, 0);
        lv_obj_set_style_pad_all(page_dot[i], 0, 0);
        lv_obj_clear_flag(page_dot[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(page_dot[i], LV_ALIGN_CENTER, page_x[i], 0);
        if (i == 0) {
            // Active: solid red, no border
            lv_obj_set_style_bg_color(page_dot[i], lv_color_hex(0xD32F2F), 0);
            lv_obj_set_style_bg_opa(page_dot[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(page_dot[i], 0, 0);
        } else {
            // Inactive: dark fill, grey ring border
            lv_obj_set_style_bg_color(page_dot[i], lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_bg_opa(page_dot[i], LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(page_dot[i], 2, 0);
            lv_obj_set_style_border_color(page_dot[i], lv_color_hex(0x666666), 0);
        }
        page_dot_lbl[i] = lv_label_create(page_dot[i]);
        lv_label_set_text_fmt(page_dot_lbl[i], "%d", i + 1);
        lv_obj_set_style_text_font(page_dot_lbl[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(page_dot_lbl[i], (i == 0) ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x999999), 0);
        lv_obj_center(page_dot_lbl[i]);
    }

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

// --- Helper: create an invisible page container ---
static lv_obj_t *create_page(lv_obj_t *parent) {
    lv_obj_t *pg = lv_obj_create(parent);
    lv_obj_set_size(pg, 800, 480 - HEADER_H);
    lv_obj_set_pos(pg, 0, HEADER_H);
    lv_obj_set_style_pad_all(pg, 0, 0);
    lv_obj_set_style_bg_color(pg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(pg, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pg, 0, 0);
    lv_obj_clear_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
    return pg;
}

// --- Helper: show/hide pages and update indicators ---
static void switch_to_page(int pg) {
    current_page = pg;
    for (int i = 0; i < NUM_PAGES; i++) {
        if (i == pg) {
            lv_obj_clear_flag(page_cont[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(page_dot[i], lv_color_hex(0xD32F2F), 0);
            lv_obj_set_style_border_width(page_dot[i], 0, 0);
            lv_obj_set_style_text_color(page_dot_lbl[i], lv_color_hex(0xFFFFFF), 0);
        } else {
            lv_obj_add_flag(page_cont[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(page_dot[i], lv_color_hex(0x1A1A1A), 0);
            lv_obj_set_style_border_width(page_dot[i], 2, 0);
            lv_obj_set_style_border_color(page_dot[i], lv_color_hex(0x666666), 0);
            lv_obj_set_style_text_color(page_dot_lbl[i], lv_color_hex(0x999999), 0);
        }
    }
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

    // --- PAGE 1: RPM, Lambda, MAP / Speed, Rail, Coolant ---
    page_cont[0] = create_page(layout);
    create_bold_gauge(page_cont[0], "ENGINE RPM", 0, 6000, "1500", "3000", "4500", 0, 0, &arc_rpm, &lbl_rpm_val);
    create_bold_gauge(page_cont[0], "LAMBDA",     0, 100,  "",     "1.00", "",     1, 0, &p1_arc_lambda, &p1_lbl_lambda);
    create_bold_gauge(page_cont[0], "MAP kPa",    0, 255,  "0",    "127",  "255",  2, 0, &p1_arc_map, &p1_lbl_map);
    create_bold_gauge(page_cont[0], "SPEED KMH",  0, 260,  "0",    "130",  "260",  0, 1, &p1_arc_duty, &p1_lbl_duty);
    create_bold_gauge(page_cont[0], "RAIL PRES",  0, 200,  "0",    "100",  "200",  1, 1, &p1_arc_rail, &p1_lbl_rail);
    create_bold_gauge(page_cont[0], "COOLANT C",  0, 120,  "0",    "60",   "120",  2, 1, &p1_arc_clt, &p1_lbl_clt);

    // --- PAGE 2: Speed, Lambda, MAP / Duty, Rail, Coolant ---
    page_cont[1] = create_page(layout);
    create_bold_gauge(page_cont[1], "SPEED KMH",  0, 260,  "0",    "130",  "260",  0, 0, &arc_speed, &lbl_speed_val);
    create_bold_gauge(page_cont[1], "LAMBDA",     0, 100,  "",     "1.00", "",     1, 0, &p2_arc_lambda, &p2_lbl_lambda);
    create_bold_gauge(page_cont[1], "MAP kPa",    0, 255,  "0",    "127",  "255",  2, 0, &p2_arc_map, &p2_lbl_map);
    create_bold_gauge(page_cont[1], "DUTY INJ %", 0, 100,  "0",    "50",   "100",  0, 1, &p2_arc_duty, &p2_lbl_duty);
    create_bold_gauge(page_cont[1], "RAIL PRES",  0, 200,  "0",    "100",  "200",  1, 1, &p2_arc_rail, &p2_lbl_rail);
    create_bold_gauge(page_cont[1], "COOLANT C",  0, 120,  "0",    "60",   "120",  2, 1, &p2_arc_clt, &p2_lbl_clt);

    // --- PAGE 3: Oil Temp, Lambda, MAP / Duty, Rail, Coolant ---
    page_cont[2] = create_page(layout);
    create_bold_gauge(page_cont[2], "OIL TEMP C", 0, 150,  "0",    "75",   "150",  0, 0, &arc_oil, &lbl_oil_val);
    create_bold_gauge(page_cont[2], "LAMBDA",     0, 100,  "",     "1.00", "",     1, 0, &p3_arc_lambda, &p3_lbl_lambda);
    create_bold_gauge(page_cont[2], "MAP kPa",    0, 255,  "0",    "127",  "255",  2, 0, &p3_arc_map, &p3_lbl_map);
    create_bold_gauge(page_cont[2], "DUTY INJ %", 0, 100,  "0",    "50",   "100",  0, 1, &p3_arc_duty, &p3_lbl_duty);
    create_bold_gauge(page_cont[2], "RAIL PRES",  0, 200,  "0",    "100",  "200",  1, 1, &p3_arc_rail, &p3_lbl_rail);
    create_bold_gauge(page_cont[2], "COOLANT C",  0, 120,  "0",    "60",   "120",  2, 1, &p3_arc_clt, &p3_lbl_clt);

    // Start on page 1
    switch_to_page(0);
}

// --- Helper: update a lambda gauge pair ---
static void update_lambda(lv_obj_t *arc, lv_obj_t *lbl, float lambda) {
    int scaled = (int)((lambda - 0.50f) * 100);
    if (scaled < 0) scaled = 0;
    if (scaled > 100) scaled = 100;
    if (arc) lv_arc_set_value(arc, scaled);
    if (lbl) {
        int w = (int)lambda;
        int d = (int)((lambda - w) * 100);
        lv_label_set_text_fmt(lbl, "%d.%02d", w, abs(d));
        if (lambda == 0.0f) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xD32F2F), 0); // Red
        } else {
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0); // White
        }
    }
}

// --- Helper: update a MAP/boost gauge pair (kPa) ---
static void update_boost(lv_obj_t *arc, lv_obj_t *lbl, int boost_kpa) {
    if (arc) lv_arc_set_value(arc, boost_kpa);
    if (lbl) lv_label_set_text_fmt(lbl, "%d", boost_kpa);
}

// --- Helper: update a duty gauge pair ---
static void update_duty(lv_obj_t *arc, lv_obj_t *lbl, float duty) {
    if (arc) lv_arc_set_value(arc, (int)duty);
    if (lbl) {
        int w = (int)duty;
        int d = (int)((duty - w) * 10);
        lv_label_set_text_fmt(lbl, "%d.%d", w, abs(d));
    }
}

// --- Helper: update a simple int gauge pair ---
static void update_int_gauge(lv_obj_t *arc, lv_obj_t *lbl, int val) {
    if (arc) lv_arc_set_value(arc, val);
    if (lbl) lv_label_set_text_fmt(lbl, "%d", val);
}

void ui_update() {
    VehicleData d;

#ifdef DEMO_MODE
    static int demo_timer = 0;
    demo_timer += 50; 
    float wave = (sinf(demo_timer * 0.001f) + 1.0f) / 2.0f;
    d.rpm = 1000 + (int)(wave * 5000); 
    d.lambda = 0.50f + (wave * 1.0f);
    d.boost = (int)(wave * 30.0f / 10);
    d.duty_injection = wave * 100.0f;
    d.fuel_rail_press = wave * 200.0f;
    d.coolant_temp = (int)(40 + (wave * 80));
    d.oil_temp = (int)(70 + (wave * 40));
    d.speed_obd = (int)(wave * 260);
    d.battery_voltage = 12.0f + wave * 2.5f;
    d.gear = (int)(wave * 6); if(d.gear==0) d.gear=1;
    d.pedal_pos = (int)(wave * 100);
    d.brake_pos = (int)((1.0f-wave) * 100);
#else
    if (pthread_mutex_trylock(&data_mutex) == 0) {
        d = v_data; 
        pthread_mutex_unlock(&data_mutex);
    } else { return; }
#endif

    // --- AUTO PAGE CYCLE DISABLED: show only page 1 ---
    // page_timer++;
    // if (page_timer >= (PAGE_CYCLE_SEC * 60)) {
    //     page_timer = 0;
    //     switch_to_page((current_page + 1) % NUM_PAGES);
    // }

    // --- SMOOTH INTERPOLATION toward target values ---
    sm.rpm      = smooth_lerp(sm.rpm,      (float)d.rpm,            SMOOTH_ALPHA);
    sm.speed    = smooth_lerp(sm.speed,    (float)d.speed_obd,      SMOOTH_ALPHA);
    sm.oil_temp = smooth_lerp(sm.oil_temp, (float)d.oil_temp,       SMOOTH_ALPHA);
    sm.lambda   = smooth_lerp(sm.lambda,   d.lambda,                SMOOTH_ALPHA);
    sm.boost    = smooth_lerp(sm.boost,    (float)d.boost,          SMOOTH_ALPHA);
    sm.duty     = smooth_lerp(sm.duty,     d.duty_injection,        SMOOTH_ALPHA);
    sm.rail     = smooth_lerp(sm.rail,     d.fuel_rail_press,       SMOOTH_ALPHA);
    sm.coolant  = smooth_lerp(sm.coolant,  (float)d.coolant_temp,   SMOOTH_ALPHA);
    sm.battery_voltage = smooth_lerp(sm.battery_voltage, d.battery_voltage, SMOOTH_ALPHA);

    int s_rpm     = (int)(sm.rpm + 0.5f);
    int s_speed   = (int)(sm.speed + 0.5f);
    int s_oil     = (int)(sm.oil_temp + 0.5f);
    int s_coolant = (int)(sm.coolant + 0.5f);
    int s_rail    = (int)(sm.rail + 0.5f);

    // --- PAGE 1: RPM ---
    if (arc_rpm) lv_arc_set_value(arc_rpm, s_rpm);
    if (lbl_rpm_val) {
        lv_label_set_text_fmt(lbl_rpm_val, "%d", s_rpm);
        if (s_rpm > 5000) lv_obj_set_style_text_color(lbl_rpm_val, lv_color_hex(0xD32F2F), 0);
        else lv_obj_set_style_text_color(lbl_rpm_val, lv_color_hex(0xFFFFFF), 0);
    }
    update_lambda(p1_arc_lambda, p1_lbl_lambda, sm.lambda);
    update_boost(p1_arc_map, p1_lbl_map, (int)(sm.boost + 0.5f));
    if (p1_arc_duty) lv_arc_set_value(p1_arc_duty, s_speed);
    if (p1_lbl_duty) lv_label_set_text_fmt(p1_lbl_duty, "%d", s_speed);
    update_int_gauge(p1_arc_rail, p1_lbl_rail, s_rail);
    update_int_gauge(p1_arc_clt, p1_lbl_clt, s_coolant);

    // --- PAGE 2: Speed ---
    if (arc_speed) lv_arc_set_value(arc_speed, s_speed);
    if (lbl_speed_val) lv_label_set_text_fmt(lbl_speed_val, "%d", s_speed);
    update_lambda(p2_arc_lambda, p2_lbl_lambda, sm.lambda);
    update_boost(p2_arc_map, p2_lbl_map, (int)(sm.boost + 0.5f));
    update_duty(p2_arc_duty, p2_lbl_duty, sm.duty);
    update_int_gauge(p2_arc_rail, p2_lbl_rail, s_rail);
    update_int_gauge(p2_arc_clt, p2_lbl_clt, s_coolant);

    // --- PAGE 3: Oil Temp ---
    if (arc_oil) lv_arc_set_value(arc_oil, s_oil);
    if (lbl_oil_val) {
        lv_label_set_text_fmt(lbl_oil_val, "%d", s_oil);
        if (s_oil > 120) lv_obj_set_style_text_color(lbl_oil_val, lv_color_hex(0xD32F2F), 0);
        else lv_obj_set_style_text_color(lbl_oil_val, lv_color_hex(0xFFFFFF), 0);
    }
    update_lambda(p3_arc_lambda, p3_lbl_lambda, sm.lambda);
    update_boost(p3_arc_map, p3_lbl_map, (int)(sm.boost + 0.5f));
    update_duty(p3_arc_duty, p3_lbl_duty, sm.duty);
    update_int_gauge(p3_arc_rail, p3_lbl_rail, s_rail);
    update_int_gauge(p3_arc_clt, p3_lbl_clt, s_coolant);

    // BATTERY VOLTAGE (LVGL doesn't support %f, use integer math)
    if (lbl_voltage) {
        if (sm.battery_voltage > 0.1f) {
            int v_whole = (int)sm.battery_voltage;
            int v_dec = (int)((sm.battery_voltage - v_whole) * 10);
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