#include "display.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"

#include "config.h"
#include "pins.h"

static const char* TAG = "display";

static constexpr uint8_t SSD1306_SET_CONTRAST = 0x81;

static esp_lcd_panel_io_handle_t io    = nullptr;
static esp_lcd_panel_handle_t    panel = nullptr;

bool display_init(i2c_master_bus_handle_t bus) {
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr            = I2C_ADDR_OLED;
    io_cfg.scl_speed_hz        = 400000;
    io_cfg.control_phase_bytes = 1;  // SSD1306: control byte before each transfer
    io_cfg.dc_bit_offset       = 6;  // D/C# is bit 6 of the control byte
    io_cfg.lcd_cmd_bits        = 8;
    io_cfg.lcd_param_bits      = 8;

    esp_lcd_panel_ssd1306_config_t ssd_cfg = {};
    ssd_cfg.height = OLED_HEIGHT;

    esp_lcd_panel_dev_config_t panel_cfg = {};
    panel_cfg.reset_gpio_num = -1;
    panel_cfg.bits_per_pixel = 1;
    panel_cfg.vendor_config  = &ssd_cfg;

    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io);
    if (err == ESP_OK) err = esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel);
    if (err == ESP_OK) err = esp_lcd_panel_reset(panel);
    if (err == ESP_OK) err = esp_lcd_panel_init(panel);
    if (err == ESP_OK && OLED_FLIP) err = esp_lcd_panel_mirror(panel, true, true);
    if (err == ESP_OK) err = esp_lcd_panel_disp_on_off(panel, true);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OLED not responding (%s) — running without display", esp_err_to_name(err));
        panel = nullptr;
        return false;
    }
    ESP_LOGI(TAG, "SSD1306 OLED ready (0x%02X)", I2C_ADDR_OLED);
    return true;
}

void display_flush(const ui::Framebuffer& fb) {
    // The framebuffer is already in SSD1306 page layout
    if (panel) esp_lcd_panel_draw_bitmap(panel, 0, 0, ui::Framebuffer::WIDTH, ui::Framebuffer::HEIGHT, fb.data());
}

void display_set_power(bool on) {
    if (panel) esp_lcd_panel_disp_on_off(panel, on);
}

void display_set_brightness(settings::Brightness b) {
    if (!panel) return;
    uint8_t contrast = 0x7F;
    switch (b) {
        case settings::Brightness::Low:    contrast = 0x10; break;
        case settings::Brightness::Medium: contrast = 0x7F; break;
        case settings::Brightness::High:   contrast = 0xFF; break;
    }
    esp_lcd_panel_io_tx_param(io, SSD1306_SET_CONTRAST, &contrast, 1);
}
