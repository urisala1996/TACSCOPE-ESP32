/* Shared phosphor-green palette and scope geometry, used by the radar
 * presentation and the provisioning screen so both have the same look. */
#pragma once
#include "gfx.h"

/* scope center + outer bezel radius (the round panel is 240x240) */
#define CX       120
#define CY       120
#define BEZEL_R  116

/* palette (phosphor green / amber accents) */
#define C_BG       gfx_rgb(0, 10, 2)
#define C_GRID     gfx_rgb(0, 70, 24)
#define C_GRID_DIM gfx_rgb(0, 44, 14)
#define C_TEXT     gfx_rgb(0, 200, 80)
#define C_TEXT_DIM gfx_rgb(0, 110, 45)
#define C_BLIP     gfx_rgb(60, 255, 120)
#define C_TRAIL    gfx_rgb(0, 120, 50)
#define C_SEL      gfx_rgb(255, 190, 40)
#define C_ALERT    gfx_rgb(255, 60, 40)
