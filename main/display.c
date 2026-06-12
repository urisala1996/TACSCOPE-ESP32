/* GC9A01 240x240 round LCD on the ESP32-2424S012 board.
 * Pins (from the vendor demo): SCLK=6 MOSI=7 DC=2 CS=10, no RST/MISO,
 * backlight on GPIO3 (active high), panel needs color inversion. */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_check.h"
#include "display.h"
#include "gfx.h"
#include "app_config.h"

#define PIN_SCLK 6
#define PIN_MOSI 7
#define PIN_DC   2
#define PIN_CS   10
#define PIN_BL   3

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;

static bool flush_done_cb(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *user)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &hp);
    return hp == pdTRUE;
}

static void backlight_init(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&tcfg);
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = (255 * BACKLIGHT_PCT) / 100,
        .hpoint = 0,
    };
    ledc_channel_config(&ccfg);
}

esp_err_t display_init(void)
{
    s_flush_done = xSemaphoreCreateBinary();

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = GFX_W * GFX_H * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_handle_t io;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 80 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
        .on_color_trans_done = flush_done_cb,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &io),
                        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
#if DISPLAY_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &s_panel),
                        TAG, "panel");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    /* This panel's column scan is reversed vs. our framebuffer, so the image
     * comes out left-right mirrored. Flip the X axis to correct it. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");

    backlight_init();
    return ESP_OK;
}

void display_flush(void)
{
    /* The framebuffer is stored as GFX_BANDS horizontal bands; push each in
     * turn. Wait for each transfer before issuing the next so the bands can
     * share the panel-IO transaction queue without overrunning it. */
    for (int b = 0; b < GFX_BANDS; b++) {
        int y0 = b * GFX_BAND_H;
        esp_lcd_panel_draw_bitmap(s_panel, 0, y0, GFX_W, y0 + GFX_BAND_H,
                                  gfx_band(b));
        xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(200));
    }
}
