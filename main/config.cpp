#include "config.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "config";

bool config_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition issue, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "NVS initialised");
    return true;
}

bool config_load(clock_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No config found in NVS");
        return false;
    }

    size_t len;
    bool ok = true;

    len = sizeof(cfg->wifi_ssid);
    if (nvs_get_str(handle, NVS_KEY_WIFI_SSID, cfg->wifi_ssid, &len) != ESP_OK) ok = false;

    len = sizeof(cfg->wifi_pass);
    if (nvs_get_str(handle, NVS_KEY_WIFI_PASS, cfg->wifi_pass, &len) != ESP_OK) ok = false;

    len = sizeof(cfg->ntp_server);
    if (nvs_get_str(handle, NVS_KEY_NTP_SERVER, cfg->ntp_server, &len) != ESP_OK) {
        strncpy(cfg->ntp_server, DEFAULT_NTP_SERVER, sizeof(cfg->ntp_server));
    }

    int8_t offset = DEFAULT_UTC_OFFSET;
    if (nvs_get_i8(handle, NVS_KEY_UTC_OFFSET, &offset) == ESP_OK) {
        cfg->utc_offset = offset;
    } else {
        cfg->utc_offset = DEFAULT_UTC_OFFSET;
    }

    int8_t brightness = DEFAULT_BRIGHTNESS;
    if (nvs_get_i8(handle, NVS_KEY_BRIGHTNESS, &brightness) == ESP_OK) {
        cfg->brightness = brightness;
    } else {
        cfg->brightness = DEFAULT_BRIGHTNESS;
    }

    nvs_close(handle);

    if (ok) {
        ESP_LOGI(TAG, "Config loaded — SSID: %s, NTP: %s, UTC offset: %d, Brightness: %d",
                 cfg->wifi_ssid, cfg->ntp_server, cfg->utc_offset, cfg->brightness);
    }
    return ok;
}

bool config_save(const clock_config_t *cfg)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing");
        return false;
    }

    bool ok = true;
    if (nvs_set_str(handle, NVS_KEY_WIFI_SSID,  cfg->wifi_ssid)          != ESP_OK) ok = false;
    if (nvs_set_str(handle, NVS_KEY_WIFI_PASS,  cfg->wifi_pass)          != ESP_OK) ok = false;
    if (nvs_set_str(handle, NVS_KEY_NTP_SERVER, cfg->ntp_server)         != ESP_OK) ok = false;
    if (nvs_set_i8(handle,  NVS_KEY_UTC_OFFSET, (int8_t)cfg->utc_offset) != ESP_OK) ok = false;
    if (nvs_set_i8(handle,  NVS_KEY_BRIGHTNESS, (int8_t)cfg->brightness) != ESP_OK) ok = false;

    if (ok) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Config saved");
    } else {
        ESP_LOGE(TAG, "Config save failed");
    }

    nvs_close(handle);
    return ok;
}

void config_erase(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
        ESP_LOGI(TAG, "Config erased");
    }
}

bool config_has_wifi(void)
{
    clock_config_t cfg;
    if (!config_load(&cfg)) return false;
    return strlen(cfg.wifi_ssid) > 0;
}