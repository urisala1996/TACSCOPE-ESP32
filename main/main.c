/* TACSCOPE — military-style live air traffic radar
 * Board: ESP32-2424S012 (ESP32-C3 + 1.28" round GC9A01 + CST816D touch)
 * Edit main/app_config.h before flashing.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "display.h"
#include "gfx.h"
#include "touch.h"
#include "wifi.h"
#include "adsb.h"
#include "radar.h"
#include "app_config.h"

static const char *TAG = "main";

static void boot_pause(void) { vTaskDelay(pdMS_TO_TICKS(180)); }

void app_main(void)
{
    /* Allocate the framebuffer first, while heap is unfragmented. */
    ESP_ERROR_CHECK(gfx_init() ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(display_init());

    radar_boot_begin();
    boot_pause();
    radar_boot_line("PWR BUS", "OK", false);
    boot_pause();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    radar_boot_line("NVS STORE", "OK", false);
    boot_pause();

    if (touch_init() == ESP_OK)
        radar_boot_line("TOUCH PNL", "OK", false);
    else
        radar_boot_line("TOUCH PNL", "FAIL", true);
    boot_pause();

    radar_boot_line("RF CAL", "OK", false);
    boot_pause();

    wifi_start();
    radar_boot_line("WIFI LINK", "....", false);
    bool connected = wifi_wait_connected(20000);
    radar_boot_line("WIFI LINK", connected ? "OK" : "FAIL", !connected);
    if (!connected) {
        radar_boot_line("CHECK CONFIG", "!!", true);
        ESP_LOGE(TAG, "WiFi failed; check app_config.h. Retrying in bg.");
        /* keep going — wifi.c retries forever, scope shows LINK LOST */
    }
    boot_pause();

    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp);
    radar_boot_line("ZULU CLOCK", "SYNC", false);
    boot_pause();

    adsb_start();
    radar_boot_line("DATALINK", "ACQ", false);
    vTaskDelay(pdMS_TO_TICKS(700));

    radar_init();
    ESP_LOGI(TAG, "scope running");

    for (;;) {
        radar_tick();           /* render + flush, ~12 ms of SPI inside */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
