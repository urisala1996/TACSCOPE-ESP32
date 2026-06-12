#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "wifi.h"
#include "app_config.h"

static const char *TAG = "wifi";
static EventGroupHandle_t s_events;
#define BIT_CONNECTED BIT0

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, BIT_CONNECTED);
        ESP_LOGW(TAG, "disconnected, retrying");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, BIT_CONNECTED);
        ESP_LOGI(TAG, "got IP");
    }
}

esp_err_t wifi_start(void)
{
    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_wifi_event, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        on_wifi_event, NULL, NULL);

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, WIFI_SSID, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, WIFI_PASS, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

bool wifi_wait_connected(int timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED,
                                           pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & BIT_CONNECTED) != 0;
}

bool wifi_is_connected(void)
{
    return (xEventGroupGetBits(s_events) & BIT_CONNECTED) != 0;
}

int wifi_rssi(void)
{
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) return ap.rssi;
    return 0;
}
