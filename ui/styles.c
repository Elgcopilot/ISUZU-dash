#include "lvgl.h"

lv_style_t style_tile;
lv_style_t style_val;
lv_style_t style_unit;

void styles_init() {
    // Glass Tile Style
    lv_style_init(&style_tile);
    lv_style_set_bg_color(&style_tile, lv_color_hex(0x1a1a1a));
    lv_style_set_bg_opa(&style_tile, LV_OPA_80);
    lv_style_set_border_width(&style_tile, 1);
    lv_style_set_border_color(&style_tile, lv_color_hex(0x444444));
    lv_style_set_radius(&style_tile, 6);

    // Value Font (Large)
    lv_style_init(&style_val);
    lv_style_set_text_color(&style_val, lv_color_white());
    lv_style_set_text_font(&style_val, &lv_font_montserrat_28);

    // Unit Font (Orange)
    lv_style_init(&style_unit);
    lv_style_set_text_color(&style_unit, lv_color_hex(0xff6600));
    lv_style_set_text_font(&style_unit, &lv_font_montserrat_14);
}
