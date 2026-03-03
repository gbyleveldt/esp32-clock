#include "touch.h"
#include "clock_face.h"
#include "esp_log.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_lvgl_port.h"
#include "driver/i2c.h"
#include "display.h"

static const char* TAG = "touch";

#define TOUCH_SDA       (gpio_num_t)4
#define TOUCH_SCL       (gpio_num_t)5
#define TOUCH_INT       (gpio_num_t)0
#define TOUCH_RST       (gpio_num_t)1
#define TOUCH_I2C_PORT  I2C_NUM_0
#define TOUCH_I2C_FREQ  400000

void touch_init(void)
{
    // Initialise I2C bus
    i2c_config_t i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed = TOUCH_I2C_FREQ
        },
    .clk_flags = 0
    };
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_I2C_PORT, &i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(TAG, "I2C bus initialised");

    // Initialise CST816S
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        }
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_I2C_PORT,
        &tp_io_cfg,
        &tp_io_handle
    ));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = 240,
        .y_max = 240,
        .rst_gpio_num = TOUCH_RST,
        .int_gpio_num = TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        }
    };

    esp_lcd_touch_handle_t tp_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &tp_handle));
    ESP_LOGI(TAG, "CST816S initialised");

    // Register with LVGL
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = display_get_handle(),
        .handle = tp_handle,
    };
    lvgl_port_add_touch(&touch_cfg);
    ESP_LOGI(TAG, "Touch registered with LVGL");
}