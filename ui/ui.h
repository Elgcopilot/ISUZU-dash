#ifndef UI_H
#define UI_H

#include "lvgl.h"

typedef enum {
	UI_NAV_UP,
	UI_NAV_DOWN,
	UI_NAV_LEFT,
	UI_NAV_RIGHT,
	UI_NAV_ENTER,
} UiNavigationCommand;

void ui_init(void);
void ui_update(void);
void ui_request_navigation(UiNavigationCommand command);

#endif
