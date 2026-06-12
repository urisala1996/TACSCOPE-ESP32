/* Wi-Fi based geolocation. Scans the surrounding access points and asks an
 * MLS-compatible service (beaconDB by default) for the device location.
 * Returns ESP_OK and fills lat/lon on success; the caller falls back to the
 * configured default otherwise. Must be called while connected as STA. */
#pragma once
#include "esp_err.h"

esp_err_t geoloc_resolve(double *lat, double *lon);
