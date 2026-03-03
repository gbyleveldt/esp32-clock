#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ── AP mode defaults ──────────────────────────────────────────────────────────
#define AP_SSID                 "ESP32-Clock-Setup"
#define AP_IP                   "192.168.4.1"

// ── NVS keys ─────────────────────────────────────────────────────────────────
#define NVS_NAMESPACE           "clock_cfg"
#define NVS_KEY_WIFI_SSID       "wifi_ssid"
#define NVS_KEY_WIFI_PASS       "wifi_pass"
#define NVS_KEY_NTP_SERVER      "ntp_server"
#define NVS_KEY_UTC_OFFSET      "utc_offset"
#define NVS_KEY_BRIGHTNESS      "brightness"

// ── Defaults ──────────────────────────────────────────────────────────────────
#define DEFAULT_NTP_SERVER      "pool.ntp.org"
#define DEFAULT_UTC_OFFSET      2
#define DEFAULT_BRIGHTNESS      100

// ── Backlight PWM ─────────────────────────────────────────────────────────────
#define BACKLIGHT_PIN           3
#define BACKLIGHT_MIN_PCT       50
#define BACKLIGHT_MAX_PCT       100

// ── Config data structure ─────────────────────────────────────────────────────
typedef struct {
    char wifi_ssid[64];
    char wifi_pass[64];
    char ntp_server[64];
    int  utc_offset;
    int  brightness;
} clock_config_t;

// ── Function declarations ─────────────────────────────────────────────────────
bool        config_init(void);
bool        config_load(clock_config_t *cfg);
bool        config_save(const clock_config_t *cfg);
void        config_erase(void);
bool        config_has_wifi(void);

#ifdef __cplusplus
}
#endif