/* Minimal CST816D capacitive touch driver (polled, no INT line).
 * Board wiring: SDA=4 SCL=5 RST=1 INT=0, I2C address 0x15. */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "touch.h"

#define PIN_SDA   4
#define PIN_SCL   5
#define PIN_RST   1
#define CST_ADDR  0x15

static const char *TAG = "touch";
static i2c_master_dev_handle_t s_dev;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 50);
}

esp_err_t touch_init(void)
{
    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &bus), TAG, "bus");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = CST_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_cfg, &s_dev),
                        TAG, "dev");

    /* disable auto entry into low-power mode (vendor demo does the same) */
    reg_write(0xFE, 0xFF);
    return ESP_OK;
}

bool touch_read(uint16_t *x, uint16_t *y)
{
    uint8_t d[5]; /* 0x02 fingers, 0x03 xH, 0x04 xL, 0x05 yH, 0x06 yL */
    if (reg_read(0x02, d, sizeof(d)) != ESP_OK) return false;
    if ((d[0] & 0x0F) == 0) return false;
    *x = ((d[1] & 0x0F) << 8) | d[2];
    *y = ((d[3] & 0x0F) << 8) | d[4];
    return true;
}
