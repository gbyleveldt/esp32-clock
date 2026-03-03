#include "display.h"
#include "config.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"

static const char* TAG = "display";
static lv_disp_t *s_disp_handle = NULL;

#define LCD_WIDTH       240
#define LCD_HEIGHT      240
#define LCD_SCLK        6
#define LCD_MOSI        7
#define LCD_CS          10
#define LCD_DC          2
#define LCD_RST         -1
#define LCD_SPI_CLOCK   (40 * 1000 * 1000)

// ── LEDC PWM config ───────────────────────────────────────────────────────────
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_0
#define LEDC_DUTY_RES   LEDC_TIMER_8_BIT   // 0-255
#define LEDC_FREQUENCY  5000               // 5kHz — above visible flicker

static void backlight_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_DUTY_RES,
        .timer_num       = LEDC_TIMER,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t channel_cfg = {
        .gpio_num   = BACKLIGHT_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .duty       = 255,   // Start at full brightness
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_cfg));
    ESP_LOGI(TAG, "Backlight PWM initialised");
}

void display_set_brightness(int percent)
{
    if (percent < BACKLIGHT_MIN_PCT) percent = BACKLIGHT_MIN_PCT;
    if (percent > BACKLIGHT_MAX_PCT) percent = BACKLIGHT_MAX_PCT;
    uint32_t duty = (uint32_t)(percent * 255 / 100);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

lv_disp_t* display_get_handle(void)
{
    return s_disp_handle;
}

void display_init(void)
{
    backlight_init();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t)
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .pclk_hz = LCD_SPI_CLOCK,
        .trans_queue_depth = 10,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_RST,
        .rgb_endian = LCD_RGB_ENDIAN_BGR,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_cfg, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));  // Mirror X
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * 40,
        .double_buffer = false,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        }
    };
    s_disp_handle = lvgl_port_add_disp(&disp_cfg);
    ESP_LOGI(TAG, "Display initialised");
}