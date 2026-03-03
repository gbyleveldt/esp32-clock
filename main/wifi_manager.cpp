#include "wifi_manager.h"
#include "config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_http_server.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "nvs_flash.h"
#include <string.h>
#include <time.h>

static const char* TAG = "wifi_manager";

static wifi_state_cb_t    s_state_cb  = NULL;
static clock_config_t     s_cfg       = {};
static esp_netif_t       *s_sta_netif = NULL;
static esp_netif_t       *s_ap_netif  = NULL;
static httpd_handle_t     s_server    = NULL;

// ── HTML config page ─────────────────────────────────────────────────────────

static const char SAVE_PAGE[] = R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Saved</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #1a1a2e;
            color: #ffffff;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
        }
        .card {
            background: #16213e;
            padding: 2rem;
            border-radius: 12px;
            width: 320px;
            text-align: center;
        }
        h1 { color: #4caf50; }
        p  { color: #a0a0b0; }
    </style>
</head>
<body>
    <div class="card">
        <h1>✓ Saved!</h1>
        <p>Settings saved. The clock is restarting and will connect to your WiFi.</p>
    </div>
</body>
</html>
)rawhtml";

// ── HTTP handlers ─────────────────────────────────────────────────────────────
static esp_err_t get_root_handler(httpd_req_t *req)
{
    char *page = (char*)malloc(3072);
    if (!page) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    snprintf(page, 3072, R"rawhtml(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>ESP32 Clock Setup</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            background: #1a1a2e;
            color: #ffffff;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 100vh;
            margin: 0;
        }
        .card {
            background: #16213e;
            padding: 2rem;
            border-radius: 12px;
            width: 320px;
            box-shadow: 0 4px 20px rgba(0,0,0,0.4);
        }
        h1 {
            text-align: center;
            font-size: 1.3rem;
            margin-bottom: 1.5rem;
            color: #c0c0c0;
        }
        label {
            display: block;
            margin-bottom: 0.3rem;
            font-size: 0.85rem;
            color: #a0a0b0;
        }
        input {
            width: 100%%;
            padding: 0.6rem;
            margin-bottom: 1rem;
            border-radius: 6px;
            border: 1px solid #444;
            background: #0f3460;
            color: #fff;
            font-size: 0.95rem;
            box-sizing: border-box;
        }
        button {
            width: 100%%;
            padding: 0.75rem;
            background: #e94560;
            color: white;
            border: none;
            border-radius: 6px;
            font-size: 1rem;
            cursor: pointer;
        }
        button:hover { background: #c73652; }
        .note {
            text-align: center;
            font-size: 0.75rem;
            color: #666;
            margin-top: 1rem;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>&#9201; ESP32 Clock Setup</h1>
        <form action="/save" method="POST">
            <label>WiFi Network (SSID)</label>
            <input type="text" name="ssid" value="%s" placeholder="Your WiFi name" required>

            <label>WiFi Password</label>
            <input type="password" name="pass" placeholder="Leave blank to keep current">

            <label>NTP Server</label>
            <input type="text" name="ntp" value="%s">

            <label>UTC Offset (e.g. 2 for UTC+2, -5 for UTC-5)</label>
            <input type="number" name="utc" value="%d" min="-12" max="14">

            <button type="submit">Save &amp; Restart</button>
        </form>
        <p class="note">Device will restart and connect to your WiFi.</p>
    </div>
</body>
</html>
)rawhtml",
        s_cfg.wifi_ssid,
        s_cfg.ntp_server,
        s_cfg.utc_offset
    );

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, strlen(page));
    free(page);  // Important — free it after sending
    return ESP_OK;
}

// Simple URL decode — handles %XX and + -> space
static void url_decode(char *dst, const char *src, size_t max_len)
{
    size_t i = 0;
    while (*src && i < max_len - 1) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// Extract a field value from URL-encoded POST body
static bool get_field(const char *body, const char *key, char *out, size_t out_len)
{
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return false;
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char raw[128] = {};
    if (len >= sizeof(raw)) len = sizeof(raw) - 1;
    memcpy(raw, p, len);
    url_decode(out, raw, out_len);
    return true;
}

static esp_err_t post_save_handler(httpd_req_t *req)
{
    char body[512] = {};
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = '\0';

    clock_config_t new_cfg = {};

    get_field(body, "ssid", new_cfg.wifi_ssid, sizeof(new_cfg.wifi_ssid));

    // Only update password if user entered one, otherwise keep existing
    char new_pass[64] = {};
    if (get_field(body, "pass", new_pass, sizeof(new_pass)) && strlen(new_pass) > 0) {
        strncpy(new_cfg.wifi_pass, new_pass, sizeof(new_cfg.wifi_pass));
    } else {
        strncpy(new_cfg.wifi_pass, s_cfg.wifi_pass, sizeof(new_cfg.wifi_pass));
    }

    if (!get_field(body, "ntp", new_cfg.ntp_server, sizeof(new_cfg.ntp_server))) {
        strncpy(new_cfg.ntp_server, DEFAULT_NTP_SERVER, sizeof(new_cfg.ntp_server));
    }

    char utc_str[8] = "0";
    get_field(body, "utc", utc_str, sizeof(utc_str));
    new_cfg.utc_offset = atoi(utc_str);

    // Preserve brightness — don't reset it from the web form
    new_cfg.brightness = s_cfg.brightness;

    ESP_LOGI(TAG, "Received config — SSID: %s, NTP: %s, UTC: %d",
             new_cfg.wifi_ssid, new_cfg.ntp_server, new_cfg.utc_offset);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SAVE_PAGE, strlen(SAVE_PAGE));

    config_save(&new_cfg);

    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

    return ESP_OK;
}

// ── HTTP server ───────────────────────────────────────────────────────────────
static void start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t root = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = get_root_handler,
        .user_ctx = NULL
    };

    httpd_uri_t save = {
        .uri      = "/save",
        .method   = HTTP_POST,
        .handler  = post_save_handler,
        .user_ctx = NULL
    };

    if (httpd_start(&s_server, &config) == ESP_OK) {
        httpd_register_uri_handler(s_server, &root);
        httpd_register_uri_handler(s_server, &save);
        ESP_LOGI(TAG, "Web server started");
    }
}

// ── NTP ───────────────────────────────────────────────────────────────────────
static void ntp_start(const char *server, int utc_offset)
{
    // POSIX timezone sign is inverted — UTC+2 = "UTC-2"
    char tz[16];
    if (utc_offset >= 0) {
        snprintf(tz, sizeof(tz), "UTC-%d", utc_offset);
    } else {
        snprintf(tz, sizeof(tz), "UTC+%d", -utc_offset);
    }
    setenv("TZ", tz, 1);
    tzset();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, server);
    esp_sntp_init();
    ESP_LOGI(TAG, "NTP started — server: %s, timezone: %s", server, tz);
}

// ── WiFi event handler ────────────────────────────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        if (s_state_cb) s_state_cb(WIFI_MODE_STA_CONNECTING);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        if (s_state_cb) s_state_cb(WIFI_MODE_STA_CONNECTING);
        esp_wifi_connect();

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi connected");
        if (s_state_cb) s_state_cb(WIFI_MODE_STA_CONNECTED);
        ntp_start(s_cfg.ntp_server, s_cfg.utc_offset);
    
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Device connected to AP");
    }
}

// ── Public: start AP mode ─────────────────────────────────────────────────────
void wifi_manager_start_ap(void)
{
    ESP_LOGI(TAG, "Starting AP mode");

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_AP);

    wifi_config_t ap_cfg = {};
    strncpy((char*)ap_cfg.ap.ssid, AP_SSID, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len       = strlen(AP_SSID);
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (s_state_cb) s_state_cb(WIFI_MODE_AP_ACTIVE);
    start_webserver();

    ESP_LOGI(TAG, "AP active — SSID: %s, IP: %s", AP_SSID, AP_IP);
}

// ── Public: init ──────────────────────────────────────────────────────────────
void wifi_manager_init(const clock_config_t *cfg, wifi_state_cb_t state_cb)
{
    memcpy(&s_cfg, cfg, sizeof(clock_config_t));
    s_state_cb = state_cb;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    if (strlen(cfg->wifi_ssid) == 0) {
        // No credentials — go straight to AP mode
        ESP_LOGI(TAG, "No credentials found, starting AP mode");
        esp_wifi_set_mode(WIFI_MODE_AP);

        wifi_config_t ap_cfg = {};
        strncpy((char*)ap_cfg.ap.ssid, AP_SSID, sizeof(ap_cfg.ap.ssid));
        ap_cfg.ap.ssid_len       = strlen(AP_SSID);
        ap_cfg.ap.channel        = 1;
        ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
        ap_cfg.ap.max_connection = 4;

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());

        if (s_state_cb) s_state_cb(WIFI_MODE_AP_ACTIVE);
        start_webserver();
        ESP_LOGI(TAG, "AP active — SSID: %s  IP: %s", AP_SSID, AP_IP);

    } else {
        // Credentials found — connect as STA
        ESP_LOGI(TAG, "Credentials found, connecting to %s", cfg->wifi_ssid);
        wifi_config_t sta_cfg = {};
        strncpy((char*)sta_cfg.sta.ssid,     cfg->wifi_ssid, sizeof(sta_cfg.sta.ssid));
        strncpy((char*)sta_cfg.sta.password, cfg->wifi_pass, sizeof(sta_cfg.sta.password));

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}