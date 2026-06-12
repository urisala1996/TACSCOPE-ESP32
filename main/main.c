/* TACSCOPE — military-style live air traffic radar
 * Board: ESP32-2424S012 (ESP32-C3 + 1.28" round GC9A01 + CST816D touch)
 * Wi-Fi and location are configured on-device; app_config.h holds the
 * fallbacks. Press the side (BOOT) button to (re)open the setup portal.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "display.h"
#include "gfx.h"
#include "touch.h"
#include "button.h"
#include "config.h"
#include "wifi.h"
#include "geoloc.h"
#include "provision.h"
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
    button_init();

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
    config_load();
    radar_boot_line("NVS STORE", "OK", false);
    boot_pause();

    if (touch_init() == ESP_OK)
        radar_boot_line("TOUCH PNL", "OK", false);
    else
        radar_boot_line("TOUCH PNL", "FAIL", true);
    boot_pause();

    radar_boot_line("RF CAL", "OK", false);
    boot_pause();

    /* No credentials yet -> go straight to the on-device setup portal. */
    if (!config_has_wifi()) {
        radar_boot_line("WIFI CFG", "SETUP", true);
        boot_pause();
        provision_run();            /* never returns (reboots after save) */
    }

    wifi_start();
    radar_boot_line("WIFI LINK", "....", false);
    bool connected = wifi_wait_connected(20000);
    radar_boot_line("WIFI LINK", connected ? "OK" : "FAIL", !connected);
    if (!connected) {
        radar_boot_line("BTN = SETUP", "", false);
        ESP_LOGE(TAG, "WiFi failed; press side button to reconfigure.");
        /* keep going — wifi.c retries forever, scope shows LINK LOST */
    }
    boot_pause();

    if (connected) {
#if GEOLOCATE_ENABLE
        double lat, lon;
        if (geoloc_resolve(&lat, &lon) == ESP_OK) {
            config_set_location(lat, lon);
            radar_boot_line("GEO FIX", "OK", false);
        } else {
            radar_boot_line("GEO FIX", "DFLT", false);
        }
        boot_pause();
#endif
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_netif_sntp_init(&sntp);
        radar_boot_line("ZULU CLOCK", "SYNC", false);
        boot_pause();
    }

    adsb_start();
    radar_boot_line("DATALINK", "ACQ", false);
    vTaskDelay(pdMS_TO_TICKS(700));

    radar_init();
    ESP_LOGI(TAG, "scope running");

    for (;;) {
        radar_tick();               /* render + flush, ~12 ms of SPI inside */
        if (button_poll())          /* side button -> Wi-Fi setup portal */
            provision_run();        /* never returns (reboots after save) */
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
