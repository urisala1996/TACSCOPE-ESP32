/* Side / BOOT push button (GPIO9, active-low, external 10k pull-up).
 * Polled, debounced — used to enter Wi-Fi provisioning at any time. */
#pragma once
#include <stdbool.h>

void button_init(void);
bool button_poll(void);   /* true once on each fresh debounced press */
