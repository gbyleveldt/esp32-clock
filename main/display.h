#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void display_init(void);
void display_set_brightness(int percent);
lv_disp_t* display_get_handle(void);

#ifdef __cplusplus
}
#endif