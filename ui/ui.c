#include "ui.h"
#include "styles.h"
#include "signals.h"
#include "lvgl.h"
#include "../app/gps_m9n.h"
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
// Higher value for real-time response
#define SMOOTH_ALPHA 0.5f

typedef struct {
    float rpm;
    float speed;
    float oil_temp;
    float oil_pressure;
    float lambda;
    float boost;
    float duty;
    float rail;
    float coolant;
    float battery_voltage;
    float imu_temp;
    float gps_satellites;
} SmoothedData;

static SmoothedData sm = {0};  // current smoothed display values

static float smooth_lerp(float current, float target, float alpha) {
    float diff = target - current;
    // Snap threshold widened to reduce overshoots from sensor noise
    if (diff > -1.0f && diff < 1.0f) return target;
    return current + diff * alpha;
}

// --- UI HANDLES ---
static lv_obj_t *lbl_voltage;
static lv_obj_t *batt_dot;     // Battery status indicator dot
static int blink_counter = 0;  // For blinking animation

// Page system
#define NUM_PAGES 3
#define PAGE_CYCLE_SEC 5
static int current_page = 0;  // Start on Page 1
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

// Page 2 gauges: Coolant, Air Temp, Oil Temp, Rail Pressure, Duty, GPS Sky Plot
static lv_obj_t *arc_coolant_main, *lbl_coolant_main;
static lv_obj_t *p2_arc_air_temp, *p2_lbl_air_temp;
static lv_obj_t *p2_arc_oil_temp, *p2_lbl_oil_temp;
static lv_obj_t *p2_arc_rail, *p2_lbl_rail;
static lv_obj_t *p2_arc_duty, *p2_lbl_duty;
static lv_obj_t *p2_gps_skyplot;  // Sky plot canvas
static lv_obj_t *p2_gps_title;  // GPS title with satellite count
static lv_obj_t *sat_dots[MAX_SATELLITES];    // Pre-allocated dot pool
static bool      sat_dots_visible[MAX_SATELLITES]; // fade-in/out state

// Page 3 gauges: GPS LAT, GPS LONG, GPS TIME, G-Force LAT, G-Force LONG, Delta Time
static lv_obj_t *arc_gps_lat, *lbl_gps_lat;
static lv_obj_t *p3_arc_gps_long, *p3_lbl_gps_long;
static lv_obj_t *p3_arc_gps_time, *p3_lbl_gps_time;
static lv_obj_t *p3_arc_gforce_lat, *p3_lbl_gforce_lat;
static lv_obj_t *p3_arc_gforce_long, *p3_lbl_gforce_long;
static lv_obj_t *p3_arc_clt, *p3_lbl_clt;
static lv_obj_t *p3_lbl_delta_time;  // Delta time display

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

// --- Helper: create simple text display (no gauge) ---
static void create_text_display(lv_obj_t *parent, const char *title, int col, int row, lv_obj_t **lbl_out) {
    int x_pos = GAUGE_GAP + (col * (GAUGE_WIDTH + GAUGE_GAP));
    int y_pos = GAUGE_GAP + (row * (GAUGE_HEIGHT + GAUGE_GAP));
    
    // Container
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
    
    // Title
    lv_obj_t *l_title = lv_label_create(cont);
    lv_label_set_text(l_title, title);
    lv_obj_set_style_text_font(l_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l_title, lv_color_hex(0x888888), 0);
    lv_obj_align(l_title, LV_ALIGN_TOP_MID, 0, 15);
    
    // Value label (large centered text)
    lv_obj_t *lbl_val = lv_label_create(cont);
    lv_label_set_text(lbl_val, "--");
    lv_obj_set_style_text_font(lbl_val, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(lbl_val, LV_ALIGN_CENTER, 0, 10);
    
    *lbl_out = lbl_val;
}

// Satellite dot opacity animation helpers
static void sat_opa_cb(void *obj, int32_t val) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)val, 0);
}
static void fade_dot(lv_obj_t *dot, lv_opa_t from, lv_opa_t to, uint32_t dur) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, sat_opa_cb);
    lv_anim_set_values(&a, (int32_t)from, (int32_t)to);
    lv_anim_set_time(&a, dur);
    lv_anim_start(&a);
}

// --- GPS Sky Plot Functions ---
// Convert polar coordinates (azimuth, elevation) to Cartesian (x, y)
static void polar_to_cartesian(int azimuth, int elevation, int center_x, int center_y, int radius, int *x, int *y) {
    // Elevation: 0° = horizon (radius), 90° = zenith (center)
    // Azimuth: 0° = North (top), 90° = East (right), 180° = South (bottom), 270° = West (left)
    
    float r = radius * (1.0f - (float)elevation / 90.0f);  // Distance from center
    float angle_rad = (float)(azimuth - 90) * PI / 180.0f;  // Rotate so 0° is North (top)
    
    *x = center_x + (int)(r * cosf(angle_rad));
    *y = center_y + (int)(r * sinf(angle_rad));
}

// Create GPS Sky Plot widget
static lv_obj_t *create_gps_skyplot(lv_obj_t *parent, int col, int row) {
    int x_pos = GAUGE_GAP + (col * (GAUGE_WIDTH + GAUGE_GAP));
    int y_pos = GAUGE_GAP + (row * (GAUGE_HEIGHT + GAUGE_GAP));
    
    // Container
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
    
    // Title with satellite count
    p2_gps_title = lv_label_create(cont);
    lv_label_set_text(p2_gps_title, "GPS SATELLITES : 0");
    lv_obj_set_style_text_font(p2_gps_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(p2_gps_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(p2_gps_title, LV_ALIGN_BOTTOM_MID, 0, -5);
    
    int plot_size = 170;  // Sky plot diameter
    int cx = GAUGE_WIDTH / 2;
    int cy = GAUGE_HEIGHT / 2 - 5;  // Center with title at bottom
    int radius = plot_size / 2;
    
    // Draw 3 elevation rings (30°, 60°, 90°)
    for (int i = 1; i <= 3; i++) {
        lv_obj_t *ring = lv_obj_create(cont);
        int ring_radius = (radius * i) / 3;
        lv_obj_set_size(ring, ring_radius * 2, ring_radius * 2);
        lv_obj_align(ring, LV_ALIGN_CENTER, 0, -5);
        lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ring, 1, 0);
        lv_obj_set_style_border_color(ring, lv_color_hex(0x444444), 0);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // Draw N, E, S, W axis lines
    lv_point_t line_points_h[2] = {{cx - radius, cy}, {cx + radius, cy}};
    lv_point_t line_points_v[2] = {{cx, cy - radius}, {cx, cy + radius}};
    
    lv_obj_t *line_h = lv_line_create(cont);
    lv_line_set_points(line_h, line_points_h, 2);
    lv_obj_set_style_line_width(line_h, 1, 0);
    lv_obj_set_style_line_color(line_h, lv_color_hex(0x444444), 0);
    
    lv_obj_t *line_v = lv_line_create(cont);
    lv_line_set_points(line_v, line_points_v, 2);
    lv_obj_set_style_line_width(line_v, 1, 0);
    lv_obj_set_style_line_color(line_v, lv_color_hex(0x444444), 0);
    
    // Pre-allocate satellite dot pool — hidden initially, updated in-place
    for (int i = 0; i < MAX_SATELLITES; i++) {
        sat_dots[i] = lv_obj_create(cont);
        lv_obj_set_size(sat_dots[i], 8, 8);
        lv_obj_set_pos(sat_dots[i], cx - 4, cy - 4);
        lv_obj_set_style_opa(sat_dots[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(sat_dots[i], lv_color_hex(0xFF3300), 0);
        lv_obj_set_style_bg_opa(sat_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(sat_dots[i], 0, 0);
        lv_obj_set_style_radius(sat_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(sat_dots[i], LV_OBJ_FLAG_SCROLLABLE);
        sat_dots_visible[i] = false;
    }

    return cont;
}

// Update GPS Sky Plot with satellite data
static void update_gps_skyplot(lv_obj_t *skyplot, GPSData *gps_data) {
    if (!skyplot) return;

    int cx = GAUGE_WIDTH / 2;
    int cy = GAUGE_HEIGHT / 2 - 5;
    int radius = 85;

    // Title: keep last known count if GPS momentarily reports 0
    static int last_num_sats = 0;
    if (gps_data->num_satellites > 0)
        last_num_sats = gps_data->num_satellites;
    if (p2_gps_title) {
        static int displayed_num = -1;
        if (last_num_sats != displayed_num) {
            displayed_num = last_num_sats;
            char buf[64];
            snprintf(buf, sizeof(buf), "GPS SATELLITES : %d", last_num_sats);
            lv_label_set_text(p2_gps_title, buf);
        }
    }

    // Update each dot in-place — no delete/recreate, smooth fade transitions
    for (int i = 0; i < MAX_SATELLITES; i++) {
        if (!sat_dots[i]) continue;

        if (gps_data->sats[i].prn == 0) {
            // No satellite in this slot — fade out
            if (sat_dots_visible[i]) {
                fade_dot(sat_dots[i], LV_OPA_COVER, LV_OPA_TRANSP, 600);
                sat_dots_visible[i] = false;
            }
            continue;
        }

        // Compute pixel position from polar coords
        int x, y;
        polar_to_cartesian(gps_data->sats[i].azimuth, gps_data->sats[i].elevation,
                           cx, cy, radius, &x, &y);

        // Clip: hide dots that fall outside the circle boundary
        int dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > radius * radius) {
            if (sat_dots_visible[i]) {
                fade_dot(sat_dots[i], LV_OPA_COVER, LV_OPA_TRANSP, 400);
                sat_dots_visible[i] = false;
            }
            continue;
        }

        // Color: Green = used in fix, Yellow = tracked/medium signal, Red = weak/not used
        lv_color_t color;
        int sat_size;
        if (gps_data->sats[i].used) {
            color    = lv_color_hex(0x00DD44);  // Green — in position fix
            sat_size = 8 + (gps_data->sats[i].cn0 / 12);
        } else if (gps_data->sats[i].cn0 >= 15) {
            color    = lv_color_hex(0xFFCC00);  // Yellow — tracked, medium signal
            sat_size = 6 + (gps_data->sats[i].cn0 / 15);
        } else {
            color    = lv_color_hex(0xFF3300);  // Red — weak or not used
            sat_size = 6;
        }
        if (sat_size > 14) sat_size = 14;

        // Update position, size, color in-place
        lv_obj_set_size(sat_dots[i], sat_size, sat_size);
        lv_obj_set_pos(sat_dots[i], x - sat_size / 2, y - sat_size / 2);
        lv_obj_set_style_bg_color(sat_dots[i], color, 0);

        // Fade in if this dot was previously hidden
        if (!sat_dots_visible[i]) {
            lv_obj_set_style_opa(sat_dots[i], LV_OPA_TRANSP, 0);
            fade_dot(sat_dots[i], LV_OPA_TRANSP, LV_OPA_COVER, 400);
            sat_dots_visible[i] = true;
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
    create_bold_gauge(page_cont[0], "BOOST kPa",  0, 300,  "0",    "150",  "300",  2, 0, &p1_arc_map, &p1_lbl_map);
    create_bold_gauge(page_cont[0], "SPEED km/h", 0, 260,  "0",    "130",  "260",  0, 1, &p1_arc_duty, &p1_lbl_duty);
    create_bold_gauge(page_cont[0], "RAIL MPa",   0, 200,  "0",    "100",  "200",  1, 1, &p1_arc_rail, &p1_lbl_rail);
    create_bold_gauge(page_cont[0], "COOLANT °C", 0, 120,  "0",    "60",   "120",  2, 1, &p1_arc_clt, &p1_lbl_clt);

    // --- PAGE 2: Coolant, Air Temp, Oil Temp / Rail Pressure, Duty, GPS ---
    page_cont[1] = create_page(layout);
    create_bold_gauge(page_cont[1], "COOLANT °C", 0, 120,  "0",    "60",   "120",  0, 0, &arc_coolant_main, &lbl_coolant_main);
    create_bold_gauge(page_cont[1], "AIR TEMP °C",0, 100,  "0",    "50",   "100",  1, 0, &p2_arc_air_temp, &p2_lbl_air_temp);
    create_bold_gauge(page_cont[1], "OIL TEMP °C",0, 150,  "0",    "75",   "150",  2, 0, &p2_arc_oil_temp, &p2_lbl_oil_temp);
    create_bold_gauge(page_cont[1], "RAIL MPa",   0, 200,  "0",    "100",  "200",  0, 1, &p2_arc_rail, &p2_lbl_rail);
    create_bold_gauge(page_cont[1], "DUTY INJ %", 0, 100,  "0",    "50",   "100",  1, 1, &p2_arc_duty, &p2_lbl_duty);
    p2_gps_skyplot = create_gps_skyplot(page_cont[1], 2, 1);  // GPS Sky Plot

    // --- PAGE 3: GPS LAT, GPS LONG, GPS TIME / G-Force LAT, G-Force LONG, Delta Time ---
    page_cont[2] = create_page(layout);
    create_text_display(page_cont[2], "GPS LAT °",      0, 0, &lbl_gps_lat);
    create_text_display(page_cont[2], "GPS LONG °",     1, 0, &p3_lbl_gps_long);
    create_text_display(page_cont[2], "GPS TIME",       2, 0, &p3_lbl_gps_time);
    create_text_display(page_cont[2], "G-FORCE LAT",    0, 1, &p3_lbl_gforce_lat);
    create_text_display(page_cont[2], "G-FORCE LONG",   1, 1, &p3_lbl_gforce_long);
    create_text_display(page_cont[2], "DELTA TIME",     2, 1, &p3_lbl_delta_time);
    
    // Set unused arc pointers to NULL for page 3
    arc_gps_lat = NULL;
    p3_arc_gps_long = NULL;
    p3_arc_gps_time = NULL;
    p3_arc_gforce_lat = NULL;
    p3_arc_gforce_long = NULL;
    p3_arc_clt = NULL;

    // Start on page 2
    switch_to_page(current_page);
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
    d.oil_pressure = 2.0f + (wave * 6.0f);
    d.speed_obd = (int)(wave * 260);
    d.battery_voltage = 12.0f + wave * 2.5f;
    d.gear = (int)(wave * 6); if(d.gear==0) d.gear=1;
    d.pedal_pos = (int)(wave * 100);
    d.brake_pos = (int)((1.0f-wave) * 100);
    d.imu_temp = 20.0f + (wave * 40.0f);
    d.gps_satellites = (int)(wave * 15);
    // Demo delta time: oscillate between -1.5 and +1.5 seconds
    d.delta_time = (wave - 0.5f) * 3.0f;
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
    sm.oil_pressure = smooth_lerp(sm.oil_pressure, d.oil_pressure,  SMOOTH_ALPHA);
    sm.lambda   = smooth_lerp(sm.lambda,   d.lambda,                SMOOTH_ALPHA);
    sm.boost    = smooth_lerp(sm.boost,    (float)d.boost,          SMOOTH_ALPHA);
    sm.duty     = smooth_lerp(sm.duty,     d.duty_injection,        SMOOTH_ALPHA);
    sm.rail     = smooth_lerp(sm.rail,     d.fuel_rail_press,       SMOOTH_ALPHA);
    sm.coolant  = smooth_lerp(sm.coolant,  (float)d.coolant_temp,   SMOOTH_ALPHA);
    sm.battery_voltage = smooth_lerp(sm.battery_voltage, d.battery_voltage, SMOOTH_ALPHA);
    sm.imu_temp = smooth_lerp(sm.imu_temp, d.imu_temp,              SMOOTH_ALPHA);

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

    // --- PAGE 2: Coolant, Air Temp, Oil Temp, Rail Pressure, Duty, GPS ---
    if (arc_coolant_main) lv_arc_set_value(arc_coolant_main, s_coolant);
    if (lbl_coolant_main) {
        lv_label_set_text_fmt(lbl_coolant_main, "%d", s_coolant);
        if (s_coolant > 105) lv_obj_set_style_text_color(lbl_coolant_main, lv_color_hex(0xD32F2F), 0);
        else lv_obj_set_style_text_color(lbl_coolant_main, lv_color_hex(0xFFFFFF), 0);
    }
    // Air Temperature (Intake)
    update_int_gauge(p2_arc_air_temp, p2_lbl_air_temp, (int)(d.intake_temp));
    // Oil Temperature
    update_int_gauge(p2_arc_oil_temp, p2_lbl_oil_temp, s_oil);
    // Rail Pressure
    update_int_gauge(p2_arc_rail, p2_lbl_rail, s_rail);
    // Duty
    update_duty(p2_arc_duty, p2_lbl_duty, sm.duty);
    // GPS Sky Plot
    update_gps_skyplot(p2_gps_skyplot, &d.gps_data);

    // --- PAGE 3: GPS LAT, GPS LONG, GPS TIME, G-Force LAT, G-Force LONG, Coolant ---
    // GPS Latitude
    if (lbl_gps_lat) {
        if (d.gps_data.fix_valid) {
            int lat_whole = (int)d.gps_data.latitude;
            int lat_dec = (int)((d.gps_data.latitude - lat_whole) * 100);
            if (lat_dec < 0) lat_dec = -lat_dec;
            lv_label_set_text_fmt(lbl_gps_lat, "%d.%02d", lat_whole, lat_dec);
        } else {
            lv_label_set_text(lbl_gps_lat, "--");
        }
    }
    
    // GPS Longitude
    if (p3_lbl_gps_long) {
        if (d.gps_data.fix_valid) {
            int lon_whole = (int)d.gps_data.longitude;
            int lon_dec = (int)((d.gps_data.longitude - lon_whole) * 100);
            if (lon_dec < 0) lon_dec = -lon_dec;
            lv_label_set_text_fmt(p3_lbl_gps_long, "%d.%02d", lon_whole, lon_dec);
        } else {
            lv_label_set_text(p3_lbl_gps_long, "--");
        }
    }
    
    // GPS Time (UTC+7 for Thailand)
    if (p3_lbl_gps_time) {
        if (d.gps_data.time_valid) {
            int thai_hour = (d.gps_data.utc_hour + 7) % 24;
            lv_label_set_text_fmt(p3_lbl_gps_time, "%02d:%02d:%02d", thai_hour, d.gps_data.utc_minute, d.gps_data.utc_second);
        } else {
            lv_label_set_text(p3_lbl_gps_time, "--:--:--");
        }
    }
    
    // G-Force Lateral
    if (p3_lbl_gforce_lat) {
        int whole = (int)d.g_force_lat;
        int dec = (int)((d.g_force_lat - whole) * 10);
        if (dec < 0) dec = -dec;
        lv_label_set_text_fmt(p3_lbl_gforce_lat, "%d.%d", whole, dec);
    }
    
    // G-Force Longitudinal
    if (p3_lbl_gforce_long) {
        int whole = (int)d.g_force_long;
        int dec = (int)((d.g_force_long - whole) * 10);
        if (dec < 0) dec = -dec;
        lv_label_set_text_fmt(p3_lbl_gforce_long, "%d.%d", whole, dec);
    }
    
    // Delta Time (racing telemetry)
    if (p3_lbl_delta_time) {
        // Check if we have a valid reference lap time
        if (d.reference_lap_time > 0.0f) {
            float delta = d.delta_time;
            
            // Format: +X.XX or -X.XX
            int whole = (int)delta;
            int dec = (int)((delta - whole) * 100);
            if (dec < 0) dec = -dec;
            
            char sign = (delta >= 0) ? '+' : '-';
            if (delta < 0) whole = -whole;
            
            lv_label_set_text_fmt(p3_lbl_delta_time, "%c%d.%02d", sign, whole, dec);
            
            // Color logic for racing: Green=faster (negative), Red=slower (positive), White=neutral
            if (delta < -0.05f) {
                lv_obj_set_style_text_color(p3_lbl_delta_time, lv_color_hex(0x00FF00), 0); // Green (faster)
            } else if (delta > 0.05f) {
                lv_obj_set_style_text_color(p3_lbl_delta_time, lv_color_hex(0xFF0000), 0); // Red (slower)
            } else {
                lv_obj_set_style_text_color(p3_lbl_delta_time, lv_color_hex(0xFFFFFF), 0); // White (neutral)
            }
        } else {
            // No reference lap time - show placeholder
            lv_label_set_text(p3_lbl_delta_time, "--:--");
            lv_obj_set_style_text_color(p3_lbl_delta_time, lv_color_hex(0x888888), 0); // Grey
        }
    }

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