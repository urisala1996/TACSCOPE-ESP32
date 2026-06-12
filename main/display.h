#pragma once
#include "esp_err.h"
#include <stdint.h>

esp_err_t display_init(void);
/* Push the full framebuffer (all bands, top to bottom) to the panel.
 * Blocks until the DMA transfers complete, so the caller may draw again
 * on return. */
void display_flush(void);
