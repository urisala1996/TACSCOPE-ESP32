#pragma once

void radar_init(void);
/* Draw one frame and push it to the panel. Call continuously. */
void radar_tick(void);

/* Boot screen helpers (military-style init sequence). */
void radar_boot_begin(void);
void radar_boot_line(const char *label, const char *result, bool fail);
