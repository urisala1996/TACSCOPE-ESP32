#include <string.h>
#include "nvs.h"
#include "esp_log.h"
#include "config.h"
#include "app_config.h"

static const char *TAG = "config";
static const char *NS  = "tacscope";

static char   s_ssid[33];
static char   s_pass[64];
static double s_lat, s_lon;
static bool   s_has_location;   /* true once the user saves a location */

void config_load(void)
{
    /* defaults come from app_config.h */
    strlcpy(s_ssid, WIFI_SSID, sizeof(s_ssid));
    strlcpy(s_pass, WIFI_PASS, sizeof(s_pass));
    s_lat = HOME_LAT;
    s_lon = HOME_LON;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(s_ssid);
        nvs_get_str(h, "ssid", s_ssid, &n);   /* leaves default on NOT_FOUND */
        n = sizeof(s_pass);
        nvs_get_str(h, "pass", s_pass, &n);

        uint8_t locset = 0;
        if (nvs_get_u8(h, "locset", &locset) == ESP_OK && locset) {
            size_t sz = sizeof(double);
            nvs_get_blob(h, "lat", &s_lat, &sz);
            sz = sizeof(double);
            nvs_get_blob(h, "lon", &s_lon, &sz);
            s_has_location = true;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG, "ssid '%s' (%s), location %s",
             s_ssid, config_has_wifi() ? "configured" : "none",
             s_has_location ? "saved" : "auto/default");
}

bool        config_has_wifi(void)  { return s_ssid[0] != '\0'; }
const char *config_wifi_ssid(void) { return s_ssid; }
const char *config_wifi_pass(void) { return s_pass; }
double      config_home_lat(void)  { return s_lat; }
double      config_home_lon(void)  { return s_lon; }
bool        config_has_location(void) { return s_has_location; }

void config_set_location(double lat, double lon)
{
    s_lat = lat;
    s_lon = lon;
}

esp_err_t config_save_location(double lat, double lon)
{
    s_lat = lat;
    s_lon = lon;
    s_has_location = true;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_blob(h, "lat", &s_lat, sizeof(double));
    nvs_set_blob(h, "lon", &s_lon, sizeof(double));
    nvs_set_u8(h, "locset", 1);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved location %.6f, %.6f (%s)",
             s_lat, s_lon, esp_err_to_name(err));
    return err;
}

esp_err_t config_set_wifi(const char *ssid, const char *pass)
{
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", s_ssid);
    nvs_set_str(h, "pass", s_pass);
    err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved ssid '%s' (%s)", s_ssid, esp_err_to_name(err));
    return err;
}
