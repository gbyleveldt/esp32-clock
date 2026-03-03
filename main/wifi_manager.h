#pragma once
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MODE_STA_CONNECTING,
    WIFI_MODE_STA_CONNECTED,
    WIFI_MODE_STA_FAILED,
    WIFI_MODE_AP_ACTIVE,
} wifi_manager_state_t;

typedef void (*wifi_state_cb_t)(wifi_manager_state_t state);

void wifi_manager_init(const clock_config_t *cfg, wifi_state_cb_t state_cb);
void wifi_manager_start_ap(void);

#ifdef __cplusplus
}
#endif