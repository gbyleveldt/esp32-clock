#include "Arduino.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "display.h"
#include "clock_face.h"
#include "wifi_manager.h"
#include "config.h"
#include "touch.h"

static const char* TAG = "main";

static void wifi_state_cb(wifi_manager_state_t state)
{
    switch (state) {
        case WIFI_MODE_STA_CONNECTING:
            ESP_LOGI(TAG, "WiFi connecting...");
            break;
        case WIFI_MODE_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            break;
        case WIFI_MODE_STA_FAILED:
            ESP_LOGI(TAG, "WiFi failed");
            break;
        case WIFI_MODE_AP_ACTIVE:
            ESP_LOGI(TAG, "AP mode active");
            if (lvgl_port_lock(0)) {
                clock_face_show_ap_mode();
                lvgl_port_unlock();
            }
            break;
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "esp32_clock starting...");
    initArduino();

    config_init();

    clock_config_t cfg = {};
    if (!config_load(&cfg)) {
        ESP_LOGW(TAG, "No config in NVS, will start in AP mode");
    }

    display_init();
    display_set_brightness(cfg.brightness);

    if (lvgl_port_lock(0)) {
        clock_face_create(&cfg);
        lvgl_port_unlock();
    }

    touch_init();
    wifi_manager_init(&cfg, wifi_state_cb);

    ESP_LOGI(TAG, "Initialisation complete");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}