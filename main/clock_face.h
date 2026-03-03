#pragma once
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

void clock_face_create(clock_config_t *cfg);
void clock_face_show_settings(void);
void clock_face_show_clock(void);
void clock_face_show_ap_mode(void);

#ifdef __cplusplus
}
#endif