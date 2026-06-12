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
        nvs_close(h);
    }
    ESP_LOGI(TAG, "ssid '%s' (%s)", s_ssid,
             config_has_wifi() ? "configured" : "none");
}

bool        config_has_wifi(void)  { return s_ssid[0] != '\0'; }
const char *config_wifi_ssid(void) { return s_ssid; }
const char *config_wifi_pass(void) { return s_pass; }
double      config_home_lat(void)  { return s_lat; }
double      config_home_lon(void)  { return s_lon; }

void config_set_location(double lat, double lon)
{
    s_lat = lat;
    s_lon = lon;
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
