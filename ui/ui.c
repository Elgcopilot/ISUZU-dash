#include "ui.h"
#include "styles.h"
#include "signals.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *tv;
static lv_obj_t *lbl_map, *lbl_lambda, *lbl_duty, *lbl_rail, *lbl_clt, *lbl_oil_t;
static lv_obj_t *lbl_fuel_rate, *lbl_rpm, *lbl_speed, *lbl_boost, *lbl_pedal, *lbl_brake;
static lv_obj_t *lbl_gear, *lbl_pedal2, *lbl_boost_p1, *lbl_inj_qty;
static lv_obj_t *cont_rpm;

// Helper to create tiles (3-col layout for Page 2: 250×190)
static void create_cell(lv_obj_t *parent, const char *title, const char *unit, int col, int row, lv_obj_t **lbl_out, lv_obj_t **cont_out) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_add_style(cont, &style_tile, 0);
    lv_obj_set_size(cont, 250, 190);
    lv_obj_set_pos(cont, 15 + (col * 260), 10 + (row * 200));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(cont);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *v = lv_label_create(cont);
    lv_label_set_text(v, "--");
    lv_obj_add_style(v, &style_val, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *u = lv_label_create(cont);
    lv_label_set_text(u, unit);
    lv_obj_add_style(u, &style_unit, 0);
    lv_obj_align(u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    *lbl_out = v;
    if (cont_out) *cont_out = cont;
}

// Helper to create tiles (4-col layout for Page 1: 185×205)
static void create_cell_sm(lv_obj_t *parent, const char *title, const char *unit, int col, int row, lv_obj_t **lbl_out, lv_obj_t **cont_out) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_add_style(cont, &style_tile, 0);
    lv_obj_set_size(cont, 185, 205);
    lv_obj_set_pos(cont, 10 + (col * 197), 5 + (row * 220));
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(cont);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *v = lv_label_create(cont);
    lv_label_set_text(v, "--");
    lv_obj_add_style(v, &style_val, 0);
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *u = lv_label_create(cont);
    lv_label_set_text(u, unit);
    lv_obj_add_style(u, &style_unit, 0);
    lv_obj_align(u, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    *lbl_out = v;
    if (cont_out) *cont_out = cont;
}

void ui_init() {
    styles_init();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    
    // Header
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 800, 40);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    
    lbl_gear = lv_label_create(header);
    lv_label_set_text(lbl_gear, "N");
    lv_obj_set_style_text_font(lbl_gear, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl_gear, lv_color_hex(0x00FF00), 0);
    lv_obj_align(lbl_gear, LV_ALIGN_CENTER, 0, 0);

    tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, 800, 440);
    lv_obj_set_pos(tv, 0, 40);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);

    lv_obj_t *p1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_BOTTOM);
    lv_obj_t *p2 = lv_tileview_add_tile(tv, 0, 1, LV_DIR_TOP);

    // Page 1 — 4-col layout (185×205 tiles)
    create_cell_sm(p1, "RPM",       "rpm", 0, 0, &lbl_rpm,      &cont_rpm);
    create_cell_sm(p1, "COOLANT",   "C",   1, 0, &lbl_clt,      NULL);
    create_cell_sm(p1, "OIL TEMP",  "C",   2, 0, &lbl_oil_t,    NULL);
    create_cell_sm(p1, "BOOST",     "kPa", 3, 0, &lbl_boost_p1, NULL);
    create_cell_sm(p1, "FUEL RATE", "L/h", 0, 1, &lbl_fuel_rate, NULL);
    create_cell_sm(p1, "RAIL PRES", "MPa", 1, 1, &lbl_rail,     NULL);
    create_cell_sm(p1, "INJ QTY",   "mm3", 2, 1, &lbl_inj_qty,  NULL);
    create_cell_sm(p1, "PEDAL",     "%",   3, 1, &lbl_pedal2,   NULL);

    // Page 2
    create_cell(p2, "SPEED", "km/h", 0, 0, &lbl_speed, NULL);
    create_cell(p2, "BOOST", "psi", 1, 0, &lbl_boost, NULL);
    create_cell(p2, "LAMBDA", "L", 2, 0, &lbl_lambda, NULL);
    create_cell(p2, "PEDAL", "%", 0, 1, &lbl_pedal, NULL);
    create_cell(p2, "BRAKE", "%", 1, 1, &lbl_brake, NULL);
    create_cell(p2, "DUTY INJ", "%", 2, 1, &lbl_duty, NULL);
}

void ui_update() {
    VehicleData d;
    
    // TRYLOCK: If CAN is writing, don't freeze the UI. Just skip this frame.
    if (pthread_mutex_trylock(&data_mutex) == 0) {
        d = v_data; 
        pthread_mutex_unlock(&data_mutex);
    } else {
        return; // Skip update if busy
    }

    // STATIC CACHE: Remember old values to prevent useless redraws
    static int old_rpm = -99;
    static int old_clt = -99;
    static int old_oil_t = -99;
    static float old_fuel_rate = -99.0f;
    static float old_rail = -99.0f;
    static int old_pedal2 = -99;
    static int old_boost_p1 = -99;
    static float old_inj_qty = -99.0f;
    static int old_speed = -99;
    static int old_boost = -99;
    static float old_lambda = -99.0f;
    static int old_pedal = -99;
    static int old_brake = -99;
    static float old_duty = -99.0f;
    static int old_gear = -99;

    // Smart Updates
    if (d.rpm != old_rpm) { lv_label_set_text_fmt(lbl_rpm, "%d", d.rpm); old_rpm = d.rpm; }
    if (d.coolant_temp != old_clt) { lv_label_set_text_fmt(lbl_clt, "%d", d.coolant_temp); old_clt = d.coolant_temp; }
    if (d.oil_temp != old_oil_t) { lv_label_set_text_fmt(lbl_oil_t, "%d", d.oil_temp); old_oil_t = d.oil_temp; }
    if (d.fuel_rate != old_fuel_rate) { int fr_int = (int)d.fuel_rate; int fr_dec = (int)((d.fuel_rate - fr_int) * 10); lv_label_set_text_fmt(lbl_fuel_rate, "%d.%d", fr_int, fr_dec); old_fuel_rate = d.fuel_rate; }
    if (d.fuel_rail_press != old_rail) { int rail_int = (int)d.fuel_rail_press; int rail_dec = (int)((d.fuel_rail_press - rail_int) * 10); lv_label_set_text_fmt(lbl_rail, "%d.%d", rail_int, rail_dec); old_rail = d.fuel_rail_press; }
    if (d.pedal_pos != old_pedal2) { lv_label_set_text_fmt(lbl_pedal2, "%d", d.pedal_pos); old_pedal2 = d.pedal_pos; }
    if (d.boost != old_boost_p1) { lv_label_set_text_fmt(lbl_boost_p1, "%d", d.boost); old_boost_p1 = d.boost; }
    if (d.duty_injection != old_inj_qty) { int iq_int = (int)d.duty_injection; int iq_dec = (int)((d.duty_injection - iq_int) * 10); lv_label_set_text_fmt(lbl_inj_qty, "%d.%d", iq_int, iq_dec); old_inj_qty = d.duty_injection; }

    if (d.speed_obd != old_speed) { lv_label_set_text_fmt(lbl_speed, "%d", d.speed_obd); old_speed = d.speed_obd; }

    if (d.boost != old_boost) {
        int psi = (int)(d.boost * 0.145f); 
        lv_label_set_text_fmt(lbl_boost, "%d", psi);
        old_boost = d.boost;
    }

    if (d.lambda != old_lambda) { int lambda_int = (int)d.lambda; int lambda_dec = (int)((d.lambda - lambda_int) * 100); lv_label_set_text_fmt(lbl_lambda, "%d.%02d", lambda_int, lambda_dec); old_lambda = d.lambda; }
    if (d.pedal_pos != old_pedal) { lv_label_set_text_fmt(lbl_pedal, "%d", d.pedal_pos); old_pedal = d.pedal_pos; }
    if (d.brake_pos != old_brake) { lv_label_set_text_fmt(lbl_brake, "%d", d.brake_pos); old_brake = d.brake_pos; }
    if (d.duty_injection != old_duty) { int duty_int = (int)d.duty_injection; int duty_dec = (int)((d.duty_injection - duty_int) * 10); lv_label_set_text_fmt(lbl_duty, "%d.%d", duty_int, duty_dec); old_duty = d.duty_injection; }

    if (d.gear != old_gear) {
        if (d.gear == 0) lv_label_set_text(lbl_gear, "N");
        else lv_label_set_text_fmt(lbl_gear, "%d", d.gear);
        old_gear = d.gear;
    }

    // Warning Border (Check every frame is fine, it's cheap)
    if (cont_rpm) {
        if (d.rpm > 4500) lv_obj_set_style_border_color(cont_rpm, lv_color_hex(0xFF0000), 0);
        else lv_obj_set_style_border_color(cont_rpm, lv_color_hex(0x444444), 0);
    }
}