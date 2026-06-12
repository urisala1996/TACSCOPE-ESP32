/* Runtime configuration store.
 *
 * Wi-Fi credentials are persisted in NVS so they can be set from the on-device
 * provisioning portal; the compile-time values in app_config.h are used only as
 * defaults when nothing has been provisioned yet. The scope center (home
 * lat/lon) starts at the app_config.h default and is overwritten in RAM when
 * Wi-Fi geolocation succeeds. */
#pragma once
#include <stdbool.h>
#include "esp_err.h"

void config_load(void);              /* load creds from NVS (call after nvs init) */

bool        config_has_wifi(void);   /* true if an SSID is configured */
const char *config_wifi_ssid(void);
const char *config_wifi_pass(void);
esp_err_t   config_set_wifi(const char *ssid, const char *pass); /* save to NVS */

double config_home_lat(void);
double config_home_lon(void);
void   config_set_location(double lat, double lon); /* RAM only (geolocation) */
