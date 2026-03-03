#include "clock_face.h"
#include "config.h"
#include "display.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include <math.h>
#include <time.h>
#include "sys/time.h"

static const char* TAG = "clock_face";
static clock_config_t *s_cfg = NULL;

// ── Display dimensions ────────────────────────────────────────────────────────
#define LCD_WIDTH       240
#define LCD_HEIGHT      240
#define CLOCK_CX        120
#define CLOCK_CY        120
#define CLOCK_RADIUS    108

// ── Screens ───────────────────────────────────────────────────────────────────
static lv_obj_t *screen_clock    = NULL;
static lv_obj_t *screen_settings = NULL;

// ── Clock hand objects ────────────────────────────────────────────────────────
static lv_obj_t *label_day  = NULL;
static lv_obj_t *label_date = NULL;
// ── Hour hand ─────────────────────────────────────────────────────────────
static lv_obj_t   *hand_hour_left  = NULL;
static lv_obj_t   *hand_hour_right = NULL;
static lv_obj_t   *hand_hour_base  = NULL;
static lv_point_t  pts_hour_left[2];
static lv_point_t  pts_hour_right[2];
static lv_point_t  pts_hour_base[2];

// ── Minute hand ───────────────────────────────────────────────────────────
static lv_obj_t   *hand_min_left   = NULL;
static lv_obj_t   *hand_min_right  = NULL;
static lv_obj_t   *hand_min_base   = NULL;
static lv_point_t  pts_min_left[2];
static lv_point_t  pts_min_right[2];
static lv_point_t  pts_min_base[2];

// ── Seconds hand ──────────────────────────────────────────────────────────
static lv_obj_t   *hand_sec_main   = NULL;
static lv_obj_t   *hand_sec_tail   = NULL;
static lv_point_t  pts_sec_main[2];
static lv_point_t  pts_sec_tail[2];

// ── Brightness state ──────────────────────────────────────────────────────────
static int s_brightness = 100;

// ── Maths helper ──────────────────────────────────────────────────────────────
static void polar_to_xy(float deg, float radius, lv_coord_t *x, lv_coord_t *y)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
    *x = (lv_coord_t)(CLOCK_CX + radius * cosf(rad));
    *y = (lv_coord_t)(CLOCK_CY + radius * sinf(rad));
}

// ── Hand update ───────────────────────────────────────────────────────────────
static void hand_base_pts(float deg, float half_width,
                          lv_coord_t *x1, lv_coord_t *y1,
                          lv_coord_t *x2, lv_coord_t *y2)
{
    float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
    *x1 = (lv_coord_t)(CLOCK_CX + half_width * sinf(rad));
    *y1 = (lv_coord_t)(CLOCK_CY + half_width * cosf(rad));
    *x2 = (lv_coord_t)(CLOCK_CX - half_width * sinf(rad));
    *y2 = (lv_coord_t)(CLOCK_CY - half_width * cosf(rad));
}

static void update_hands(int hours, int minutes, float seconds)
{
    float angle_hour = (hours % 12) * 30.0f + minutes * 0.5f;
    float angle_min  = minutes * 6.0f;
    float angle_sec  = seconds * 6.0f;

    // ── Hour hand ─────────────────────────────────────────────────────────
    lv_coord_t htx, hty, hx1, hy1, hx2, hy2;
    polar_to_xy(angle_hour, CLOCK_RADIUS * 0.55f, &htx, &hty);
    hand_base_pts(angle_hour, 5.0f, &hx1, &hy1, &hx2, &hy2);

    pts_hour_left[0]  = {hx1, hy1};  pts_hour_left[1]  = {htx, hty};
    pts_hour_right[0] = {hx2, hy2};  pts_hour_right[1] = {htx, hty};
    pts_hour_base[0]  = {hx1, hy1};  pts_hour_base[1]  = {hx2, hy2};

    lv_line_set_points(hand_hour_left,  pts_hour_left,  2);
    lv_line_set_points(hand_hour_right, pts_hour_right, 2);
    lv_line_set_points(hand_hour_base,  pts_hour_base,  2);

    // ── Minute hand ───────────────────────────────────────────────────────
    lv_coord_t mtx, mty, mx1, my1, mx2, my2;
    polar_to_xy(angle_min, CLOCK_RADIUS * 0.78f, &mtx, &mty);
    hand_base_pts(angle_min, 4.0f, &mx1, &my1, &mx2, &my2);

    pts_min_left[0]  = {mx1, my1};  pts_min_left[1]  = {mtx, mty};
    pts_min_right[0] = {mx2, my2};  pts_min_right[1] = {mtx, mty};
    pts_min_base[0]  = {mx1, my1};  pts_min_base[1]  = {mx2, my2};

    lv_line_set_points(hand_min_left,  pts_min_left,  2);
    lv_line_set_points(hand_min_right, pts_min_right, 2);
    lv_line_set_points(hand_min_base,  pts_min_base,  2);

    // ── Seconds hand ──────────────────────────────────────────────────────
    lv_coord_t stx, sty, sta_x, sta_y;
    polar_to_xy(angle_sec, CLOCK_RADIUS * 0.85f, &stx, &sty);
    polar_to_xy(angle_sec + 180.0f, CLOCK_RADIUS * 0.20f, &sta_x, &sta_y);

    pts_sec_main[0] = {CLOCK_CX, CLOCK_CY};  pts_sec_main[1] = {stx,   sty};
    pts_sec_tail[0] = {CLOCK_CX, CLOCK_CY};  pts_sec_tail[1] = {sta_x, sta_y};

    lv_line_set_points(hand_sec_main, pts_sec_main, 2);
    lv_line_set_points(hand_sec_tail, pts_sec_tail, 2);
}

// ── Clock tick ────────────────────────────────────────────────────────────────
static void clock_tick_cb(lv_timer_t *timer)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    localtime_r(&tv.tv_sec, &timeinfo);

    // Smooth seconds — add fractional second
    float smooth_sec = timeinfo.tm_sec + (tv.tv_usec / 1000000.0f);

    if (lvgl_port_lock(0)) {
        update_hands(timeinfo.tm_hour, timeinfo.tm_min, smooth_sec);

        const char* days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        lv_label_set_text(label_day, days[timeinfo.tm_wday]);

        const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
        char date_str[12];
        snprintf(date_str, sizeof(date_str), "%02d %s",
                 timeinfo.tm_mday, months[timeinfo.tm_mon]);
        lv_label_set_text(label_date, date_str);

        lvgl_port_unlock();
    }
}

// ── Screen transitions ────────────────────────────────────────────────────────
void clock_face_show_settings(void)
{
    lv_scr_load_anim(screen_settings, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}

void clock_face_show_clock(void)
{
    lv_scr_load_anim(screen_clock, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

// ── Confirmation dialog ───────────────────────────────────────────────────────
static void confirm_ap_yes_cb(lv_event_t *e)
{
    // Start AP mode
    wifi_manager_start_ap();
}

static void confirm_ap_no_cb(lv_event_t *e)
{
    lv_obj_t *dialog = (lv_obj_t*)lv_event_get_user_data(e);
    lv_obj_del(dialog);
}

static void show_ap_confirm_dialog(void)
{
    // Semi-transparent overlay
    lv_obj_t *overlay = lv_obj_create(screen_settings);
    lv_obj_set_size(overlay, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog card
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 180, 160);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Start AP Mode?");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Message
    lv_obj_t *msg = lv_label_create(card);
    lv_label_set_text(msg, "Device will restart\nfor WiFi setup.");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xa0a0b0), LV_PART_MAIN);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);

    // Cancel button
    lv_obj_t *btn_no = lv_btn_create(card);
    lv_obj_set_size(btn_no, 70, 36);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x444466), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_no, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_no, confirm_ap_no_cb, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_no = lv_label_create(btn_no);
    lv_label_set_text(lbl_no, "Cancel");
    lv_obj_center(lbl_no);

    // Yes button
    lv_obj_t *btn_yes = lv_btn_create(card);
    lv_obj_set_size(btn_yes, 70, 36);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_yes, 8, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_yes, confirm_ap_yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_yes = lv_label_create(btn_yes);
    lv_label_set_text(lbl_yes, "Yes");
    lv_obj_center(lbl_yes);
}

// ── Brightness slider callback ────────────────────────────────────────────────
static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    s_brightness = lv_slider_get_value(slider);
    display_set_brightness(s_brightness);
}

// ── AP mode button callback ───────────────────────────────────────────────────
static void ap_mode_btn_cb(lv_event_t *e)
{
    show_ap_confirm_dialog();
}

// ── Build settings screen ─────────────────────────────────────────────────────
static void create_settings_screen(void)
{
    screen_settings = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_settings, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_settings, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen_settings, LV_OBJ_FLAG_SCROLLABLE);

    // ── Settings title ──
    lv_obj_t *title = lv_label_create(screen_settings);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(0xC0C0C0), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    // ── Brightness label ──
    lv_obj_t *lbl_bright = lv_label_create(screen_settings);
    lv_label_set_text(lbl_bright, "Brightness");
    lv_obj_set_style_text_color(lbl_bright, lv_color_hex(0xa0a0b0), LV_PART_MAIN);
    lv_obj_set_pos(lbl_bright, 20, 70);

    // ── Brightness slider ──
    lv_obj_t *slider = lv_slider_create(screen_settings);
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_size(slider, 180, 12);
    lv_obj_set_pos(slider, 30, 95);
    lv_slider_set_range(slider, BACKLIGHT_MIN_PCT, BACKLIGHT_MAX_PCT);
    lv_slider_set_value(slider, s_brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x333355), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xe94560), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ── AP mode button ──
    lv_obj_t *btn_ap = lv_btn_create(screen_settings);
    lv_obj_set_size(btn_ap, 150, 35);
    lv_obj_align(btn_ap, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_bg_color(btn_ap, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_ap, 10, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_ap, ap_mode_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ap = lv_label_create(btn_ap);
    lv_label_set_text(lbl_ap, "WiFi Setup (AP Mode)");
    lv_obj_set_style_text_font(lbl_ap, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(lbl_ap);

    lv_obj_add_event_cb(screen_settings, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_RIGHT) {
            if (s_cfg != NULL) {
                s_cfg->brightness = s_brightness;
                config_save(s_cfg);
                ESP_LOGI(TAG, "Brightness saved: %d", s_brightness);
            }
            clock_face_show_clock();
        }
    }, LV_EVENT_GESTURE, NULL);
}

// ── Build AP mode screen ────────────────────────────────────────────────────────
void clock_face_show_ap_mode(void)
{
    lv_obj_t *screen_ap = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_ap, lv_color_hex(0x1A1A2E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_ap, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen_ap, LV_OBJ_FLAG_SCROLLABLE);

    // WiFi icon / title
    lv_obj_t *title = lv_label_create(screen_ap);
    lv_label_set_text(title, "WiFi Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Instruction
    lv_obj_t *inst = lv_label_create(screen_ap);
    lv_label_set_text(inst, "Connect to WiFi:");
    lv_obj_set_style_text_color(inst, lv_color_hex(0xa0a0b0), LV_PART_MAIN);
    lv_obj_align(inst, LV_ALIGN_CENTER, 0, -40);

    // SSID
    lv_obj_t *ssid = lv_label_create(screen_ap);
    lv_label_set_text(ssid, AP_SSID);
    lv_obj_set_style_text_color(ssid, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_set_style_text_font(ssid, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(ssid, LV_ALIGN_CENTER, 0, -15);

    // Then open browser instruction
    lv_obj_t *inst2 = lv_label_create(screen_ap);
    lv_label_set_text(inst2, "Then open browser:");
    lv_obj_set_style_text_color(inst2, lv_color_hex(0xa0a0b0), LV_PART_MAIN);
    lv_obj_align(inst2, LV_ALIGN_CENTER, 0, 15);

    // IP address
    lv_obj_t *ip = lv_label_create(screen_ap);
    lv_label_set_text(ip, AP_IP);
    lv_obj_set_style_text_color(ip, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_set_style_text_font(ip, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(ip, LV_ALIGN_CENTER, 0, 40);

    lv_scr_load_anim(screen_ap, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    ESP_LOGI(TAG, "AP mode screen shown");
}

// ── Build clock screen ────────────────────────────────────────────────────────
static void create_clock_screen(void)
{
    screen_clock = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_clock, lv_color_hex(0x0D1428), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_clock, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen_clock, LV_OBJ_FLAG_SCROLLABLE);

    // ── Radial gradient — concentric circles from dark outside to lighter centre ──
    // Draw largest (darkest) first, smallest (lightest) last
    typedef struct { lv_coord_t r; uint32_t color; } ring_t;
    ring_t rings[] = {
        { 120, 0x0D1428 },
        { 108, 0x0F1630 },
        {  96, 0x111833 },
        {  84, 0x131A36 },
        {  72, 0x151C39 },
        {  60, 0x171E3C },
        {  48, 0x19203E },
        {  36, 0x1B2241 },
        {  24, 0x1D2444 },
        {  12, 0x1F2647 },
    };
    for (int i = 0; i < 10; i++) {
        lv_obj_t *circle = lv_obj_create(screen_clock);
        lv_coord_t d = rings[i].r * 2;
        lv_obj_set_size(circle, d, d);
        lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(circle, lv_color_hex(rings[i].color), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(circle, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(circle, 0, LV_PART_MAIN);
        lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(circle, CLOCK_CX - rings[i].r, CLOCK_CY - rings[i].r);
    }

    static lv_point_t dot_pts[60][2];

    for (int i = 0; i < 60; i++) {
        float angle = i * 6.0f;
        lv_coord_t mx, my;
        polar_to_xy(angle, CLOCK_RADIUS * 0.91f, &mx, &my);

        dot_pts[i][0] = {mx, my};
        dot_pts[i][1] = {(lv_coord_t)(mx + 1), my};  // 1px length forces rendering

        lv_obj_t *dot = lv_line_create(screen_clock);
        lv_line_set_points(dot, dot_pts[i], 2);
        lv_obj_set_style_line_rounded(dot, true, LV_PART_MAIN);

        if (i % 15 == 0) {
            lv_obj_set_style_line_width(dot, 8, LV_PART_MAIN);
            lv_obj_set_style_line_color(dot, lv_color_white(), LV_PART_MAIN);
        } else if (i % 5 == 0) {
            lv_obj_set_style_line_width(dot, 5, LV_PART_MAIN);
            lv_obj_set_style_line_color(dot, lv_color_white(), LV_PART_MAIN);
        } else {
            lv_obj_set_style_line_width(dot, 3, LV_PART_MAIN);
            lv_obj_set_style_line_color(dot, lv_color_hex(0x8090B0), LV_PART_MAIN);
        }
    }

    // ── Outer ring border ─────────────────────────────────────────────────────
    lv_obj_t *border = lv_obj_create(screen_clock);
    lv_obj_set_size(border, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_pos(border, 0, 0);
    lv_obj_set_style_radius(border, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(border, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_color(border, lv_color_hex(0x4060A0), LV_PART_MAIN);
    lv_obj_set_style_border_width(border, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(border, 0, LV_PART_MAIN);
    lv_obj_clear_flag(border, LV_OBJ_FLAG_SCROLLABLE);

    // ── Date display ──────────────────────────────────────────────────────────
    label_day = lv_label_create(screen_clock);
    lv_obj_set_style_text_color(label_day, lv_color_hex(0xA0B0C0), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_day, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(label_day, "---");
    lv_obj_set_pos(label_day, 158, 108);

    label_date = lv_label_create(screen_clock);
    lv_obj_set_style_text_color(label_date, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_date, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_label_set_text(label_date, "--/---");
    lv_obj_set_pos(label_date, 158, 126);

    // ── Hands ─────────────────────────────────────────────────────────────────
    // ── Hour hand ─────────────────────────────────────────────────────────────
    hand_hour_left = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_hour_left, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_hour_left, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_hour_left, false, LV_PART_MAIN);

    hand_hour_right = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_hour_right, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_hour_right, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_hour_right, false, LV_PART_MAIN);

    hand_hour_base = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_hour_base, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_hour_base, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_hour_base, false, LV_PART_MAIN);

    // ── Minute hand ───────────────────────────────────────────────────────────
    hand_min_left = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_min_left, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_min_left, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_min_left, false, LV_PART_MAIN);

    hand_min_right = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_min_right, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_min_right, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_min_right, false, LV_PART_MAIN);

    hand_min_base = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_min_base, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_min_base, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_min_base, false, LV_PART_MAIN);

    // ── Seconds hand ──────────────────────────────────────────────────────────
    hand_sec_main = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_sec_main, lv_color_hex(0xFF3030), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_sec_main, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_sec_main, true, LV_PART_MAIN);

    hand_sec_tail = lv_line_create(screen_clock);
    lv_obj_set_style_line_color(hand_sec_tail, lv_color_hex(0xFF3030), LV_PART_MAIN);
    lv_obj_set_style_line_width(hand_sec_tail, 3, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(hand_sec_tail, true, LV_PART_MAIN);

    // Centre dot
    lv_obj_t *centre = lv_obj_create(screen_clock);
    lv_obj_set_size(centre, 10, 10);
    lv_obj_set_style_radius(centre, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(centre, lv_color_hex(0xFF3030), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(centre, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(centre, 0, LV_PART_MAIN);
    lv_obj_set_pos(centre, CLOCK_CX - 5, CLOCK_CY - 5);

    // ── Swipe gesture ─────────────────────────────────────────────────────────
    lv_obj_add_event_cb(screen_clock, [](lv_event_t *e) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
        if (dir == LV_DIR_LEFT) {
            clock_face_show_settings();
        }
    }, LV_EVENT_GESTURE, NULL);

    update_hands(0, 0, 0);
    lv_timer_create(clock_tick_cb, 100, NULL);
}

// ── Public entry point ────────────────────────────────────────────────────────
void clock_face_create(clock_config_t *cfg)
{
    s_cfg = cfg;
    s_brightness = cfg->brightness;
    create_clock_screen();
    create_settings_screen();
    lv_scr_load(screen_clock);
    ESP_LOGI(TAG, "Clock face created");
}