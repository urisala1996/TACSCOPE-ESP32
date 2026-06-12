/* Wi-Fi geolocation against an MLS-style /v1/geolocate endpoint.
 *
 * Mozilla Location Services shut down in 2024; beaconDB (api.beacondb.net) is a
 * free, no-API-key, community-run drop-in that speaks the same protocol:
 *   POST { "considerIp": false, "wifiAccessPoints": [ {macAddress,signalStrength} ] }
 *   200  { "location": { "lat": .., "lng": .. }, "accuracy": .. }
 * A 404 means "not found in the database" — treated as a soft failure so the
 * caller keeps the configured default location. */
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "cJSON.h"
#include "geoloc.h"
#include "wifi.h"
#include "app_config.h"

static const char *TAG = "geoloc";

#define MAX_AP 16

esp_err_t geoloc_resolve(double *lat, double *lon)
{
    /* A scan briefly drops the STA association; the wifi module reconnects. */
    wifi_scan_config_t sc = { 0 };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) {
        ESP_LOGW(TAG, "scan failed");
        return ESP_FAIL;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    if (n == 0) {
        ESP_LOGW(TAG, "no access points in range");
        return ESP_FAIL;
    }
    if (n > MAX_AP) n = MAX_AP;
    static wifi_ap_record_t recs[MAX_AP];
    esp_wifi_scan_get_ap_records(&n, recs);

    for (int i = 0; i < 50 && !wifi_is_connected(); i++)
        vTaskDelay(pdMS_TO_TICKS(100));
    if (!wifi_is_connected()) {
        ESP_LOGW(TAG, "link not restored after scan");
        return ESP_FAIL;
    }

    static char body[1024];
    int p = snprintf(body, sizeof(body),
                     "{\"considerIp\":false,\"wifiAccessPoints\":[");
    for (int i = 0; i < n; i++) {
        if (p > (int)sizeof(body) - 80) break;
        p += snprintf(body + p, sizeof(body) - p,
                      "%s{\"macAddress\":\"%02x:%02x:%02x:%02x:%02x:%02x\","
                      "\"signalStrength\":%d}",
                      i ? "," : "",
                      recs[i].bssid[0], recs[i].bssid[1], recs[i].bssid[2],
                      recs[i].bssid[3], recs[i].bssid[4], recs[i].bssid[5],
                      recs[i].rssi);
    }
    p += snprintf(body + p, sizeof(body) - p, "]}");

    esp_http_client_config_t cfg = {
        .url = GEOLOCATE_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .user_agent = "TACSCOPE-ESP32/1.0",
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return ESP_FAIL;
    esp_http_client_set_header(c, "Content-Type", "application/json");

    esp_err_t ret = ESP_FAIL;
    do {
        if (esp_http_client_open(c, p) != ESP_OK) {
            ESP_LOGW(TAG, "open failed");
            break;
        }
        if (esp_http_client_write(c, body, p) < 0) {
            ESP_LOGW(TAG, "write failed");
            break;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP %d (no fix)", status);
            break;
        }

        static char resp[256];
        int off = 0, r;
        while ((r = esp_http_client_read(c, resp + off,
                                         sizeof(resp) - 1 - off)) > 0) {
            off += r;
            if (off >= (int)sizeof(resp) - 1) break;
        }
        resp[off] = '\0';

        cJSON *root = cJSON_Parse(resp);
        if (root) {
            cJSON *loc  = cJSON_GetObjectItem(root, "location");
            cJSON *jlat = loc ? cJSON_GetObjectItem(loc, "lat") : NULL;
            cJSON *jlng = loc ? cJSON_GetObjectItem(loc, "lng") : NULL;
            if (cJSON_IsNumber(jlat) && cJSON_IsNumber(jlng)) {
                *lat = jlat->valuedouble;
                *lon = jlng->valuedouble;
                ret = ESP_OK;
                ESP_LOGI(TAG, "fix %.5f, %.5f", *lat, *lon);
            }
            cJSON_Delete(root);
        }
    } while (0);

    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return ret;
}
