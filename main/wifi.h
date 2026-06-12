#pragma once
#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_start(void);                 /* init driver, begin connecting */
bool      wifi_wait_connected(int timeout_ms);
bool      wifi_is_connected(void);
int       wifi_rssi(void);                  /* dBm, 0 if unknown */
