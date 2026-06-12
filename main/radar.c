/* TACSCOPE radar presentation: phosphor-green PPI scope with rotating
 * sweep, range rings, heading-oriented contact symbols, trails and a
 * detail readout for the selected contact. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_timer.h"
#include "gfx.h"
#include "display.h"
#include "touch.h"
#include "wifi.h"
#include "adsb.h"
#include "radar.h"
#include "app_config.h"

#define CX 120
#define CY 120
#define PLOT_R   100        /* contacts plotted inside this radius      */
#define BEZEL_R  116        /* outer tick ring                          */
#define SWEEP_PERIOD_US 4000000
#define SWEEP_TAIL_STEPS 26

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

static const int s_ranges_km[] = RANGE_STEPS_KM;
#define N_RANGES ((int)(sizeof(s_ranges_km) / sizeof(s_ranges_km[0])))

static int  s_range_idx = RANGE_STEP_DEFAULT;
static char s_selected_hex[8];
static int  s_boot_y;

/* ------------------------------------------------------------------ */

void radar_init(void)
{
    if (s_range_idx < 0 || s_range_idx >= N_RANGES) s_range_idx = 0;
    s_selected_hex[0] = '\0';
}

static uint16_t lerp_green(int num, int den)
{
    /* fade from bright sweep green to background */
    int g = 10 + (190 - 10) * num / den;
    int r = 0;
    int b = 2 + (60 - 2) * num / den;
    return gfx_rgb(r, g, b);
}

static void draw_static_scope(void)
{
    gfx_clear(C_BG);

    /* bezel tick ring: every 10 deg, heavier each 30 deg */
    for (int a = 0; a < 360; a += 10) {
        float rad = a * (float)M_PI / 180.0f;
        float s = sinf(rad), c = cosf(rad);
        int inner = (a % 30 == 0) ? BEZEL_R - 9 : BEZEL_R - 4;
        gfx_line(CX + (int)(s * inner), CY - (int)(c * inner),
                 CX + (int)(s * BEZEL_R), CY - (int)(c * BEZEL_R),
                 (a % 90 == 0) ? C_TEXT : C_GRID);
    }

    /* range rings */
    gfx_circle(CX, CY, PLOT_R, C_GRID);
    gfx_circle(CX, CY, PLOT_R * 2 / 3, C_GRID_DIM);
    gfx_circle(CX, CY, PLOT_R / 3, C_GRID_DIM);

    /* crosshair */
    gfx_line(CX - PLOT_R, CY, CX + PLOT_R, CY, C_GRID_DIM);
    gfx_line(CX, CY - PLOT_R, CX, CY + PLOT_R, C_GRID_DIM);

    /* cardinal labels just inside the plot ring */
    gfx_text_center(CX, CY - PLOT_R + 4, "N", C_TEXT, 1);
    gfx_text_center(CX, CY + PLOT_R - 11, "S", C_TEXT_DIM, 1);
    gfx_text(CX + PLOT_R - 9, CY - 3, "E", C_TEXT_DIM, 1);
    gfx_text(CX - PLOT_R + 5, CY - 3, "W", C_TEXT_DIM, 1);

    /* own position */
    gfx_line(CX - 3, CY, CX + 3, CY, C_BLIP);
    gfx_line(CX, CY - 3, CX, CY + 3, C_BLIP);
}

static void draw_sweep(void)
{
    int64_t t = esp_timer_get_time() % SWEEP_PERIOD_US;
    float ang = (float)t / SWEEP_PERIOD_US * 2.0f * (float)M_PI;

    for (int k = SWEEP_TAIL_STEPS; k >= 0; k--) {
        float a = ang - k * 0.030f;
        uint16_t col = (k == 0) ? gfx_rgb(120, 255, 160)
                                : lerp_green(SWEEP_TAIL_STEPS - k,
                                             SWEEP_TAIL_STEPS);
        gfx_line(CX, CY,
                 CX + (int)(sinf(a) * (PLOT_R - 1)),
                 CY - (int)(cosf(a) * (PLOT_R - 1)), col);
    }
}

static bool contact_screen_pos(const contact_t *ct, float scale,
                               int *px, int *py)
{
    float x = ct->east_km * scale;
    float y = ct->north_km * scale;
    if (x * x + y * y > (float)(PLOT_R - 4) * (PLOT_R - 4)) return false;
    *px = CX + (int)lroundf(x);
    *py = CY - (int)lroundf(y);
    return true;
}

static void draw_contact(const contact_t *ct, float scale, bool selected)
{
    int px, py;
    if (!contact_screen_pos(ct, scale, &px, &py)) return;

    /* trail dots, oldest dimmest */
    for (int t = 0; t < ct->trail_len; t++) {
        float tx = ct->trail_e[t] * scale;
        float ty = ct->trail_n[t] * scale;
        if (tx * tx + ty * ty > (float)PLOT_R * PLOT_R) continue;
        gfx_pixel(CX + (int)tx, CY - (int)ty,
                  t < 2 ? C_TRAIL : C_GRID_DIM);
    }

    uint16_t col = selected ? C_SEL : C_BLIP;

    if (ct->track_deg >= 0) {
        /* triangle pointing along the track */
        float h = ct->track_deg * (float)M_PI / 180.0f;
        int tipx = px + (int)lroundf(sinf(h) * 6);
        int tipy = py - (int)lroundf(cosf(h) * 6);
        float hl = h + 2.50f, hr = h - 2.50f; /* ~143 deg back corners */
        gfx_fill_tri(tipx, tipy,
                     px + (int)lroundf(sinf(hl) * 5),
                     py - (int)lroundf(cosf(hl) * 5),
                     px + (int)lroundf(sinf(hr) * 5),
                     py - (int)lroundf(cosf(hr) * 5), col);
    } else {
        gfx_fill_circle(px, py, 3, col);
    }

    if (selected) gfx_circle(px, py, 9, C_SEL);

    const char *label = ct->callsign[0] ? ct->callsign : ct->hex;
    gfx_text(px + 8, py - 9, label, selected ? C_SEL : C_TEXT_DIM, 1);
}

static void draw_top_bar(const adsb_state_t *st)
{
    time_t now;
    struct tm tm;
    char buf[20];

    gfx_text_center(CX, 18, "TACSCOPE", C_TEXT_DIM, 1);

    time(&now);
    gmtime_r(&now, &tm);
    if (tm.tm_year + 1900 >= 2020)
        snprintf(buf, sizeof(buf), "%02d:%02d:%02dZ",
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    else
        snprintf(buf, sizeof(buf), "--:--:--Z");
    gfx_text_center(CX, 28, buf, C_TEXT, 1);
}

static void draw_bottom_bar(const adsb_state_t *st)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "RNG %03dKM  CON %02d",
             s_ranges_km[s_range_idx], st->count);
    gfx_text_center(CX, 206, buf, C_TEXT, 1);

    int rssi = wifi_rssi();
    if (wifi_is_connected() && rssi != 0)
        snprintf(buf, sizeof(buf), "LNK %ddBm", rssi);
    else
        snprintf(buf, sizeof(buf), "LNK ----");
    gfx_text_center(CX, 216, buf, C_TEXT_DIM, 1);

    if (!st->link_ok || !wifi_is_connected()) {
        /* flash an alert when the data link is down */
        if ((esp_timer_get_time() / 500000) % 2 == 0)
            gfx_text_center(CX, 192, "** LINK LOST **", C_ALERT, 1);
    } else if (!st->have_data) {
        gfx_text_center(CX, 192, "ACQUIRING DATA", C_TEXT_DIM, 1);
    }
}

static void draw_selected_info(const adsb_state_t *st)
{
    if (!s_selected_hex[0]) return;
    const contact_t *sel = NULL;
    for (int i = 0; i < st->count; i++) {
        if (strcmp(st->contacts[i].hex, s_selected_hex) == 0) {
            sel = &st->contacts[i];
            break;
        }
    }
    if (!sel) return;

    char l1[28], l2[28];
    if (sel->alt_ft == ALT_GROUND)
        snprintf(l1, sizeof(l1), "%s GND",
                 sel->callsign[0] ? sel->callsign : sel->hex);
    else if (sel->alt_ft == ALT_UNKNOWN)
        snprintf(l1, sizeof(l1), "%s FL---",
                 sel->callsign[0] ? sel->callsign : sel->hex);
    else
        snprintf(l1, sizeof(l1), "%s FL%03d",
                 sel->callsign[0] ? sel->callsign : sel->hex,
                 (sel->alt_ft + 50) / 100);

    float dist = sqrtf(sel->east_km * sel->east_km +
                       sel->north_km * sel->north_km);
    float brg = atan2f(sel->east_km, sel->north_km) * 180.0f / (float)M_PI;
    if (brg < 0) brg += 360.0f;
    if (sel->gs_kt >= 0)
        snprintf(l2, sizeof(l2), "%03.0fKT BRG%03.0f %.0fKM",
                 (double)sel->gs_kt, (double)brg, (double)dist);
    else
        snprintf(l2, sizeof(l2), "---KT BRG%03.0f %.0fKM",
                 (double)brg, (double)dist);

    gfx_text_center(CX, 168, l1, C_SEL, 1);
    gfx_text_center(CX, 178, l2, C_SEL, 1);
}

/* ------------------------------------------------------------------ */
/* touch interaction: tap a blip to select, tap elsewhere to cycle range */

static void handle_touch(const adsb_state_t *st, float scale)
{
    static bool was_down;
    static int64_t last_action_us;
    uint16_t tx, ty;
    bool down = touch_read(&tx, &ty);

    if (down && !was_down &&
        esp_timer_get_time() - last_action_us > 300000) {
        last_action_us = esp_timer_get_time();

        int best = -1, best_d2 = 24 * 24;
        for (int i = 0; i < st->count; i++) {
            int px, py;
            if (!contact_screen_pos(&st->contacts[i], scale, &px, &py))
                continue;
            int dx = px - (int)tx, dy = py - (int)ty;
            int d2 = dx * dx + dy * dy;
            if (d2 < best_d2) { best_d2 = d2; best = i; }
        }
        if (best >= 0) {
            if (strcmp(s_selected_hex, st->contacts[best].hex) == 0)
                s_selected_hex[0] = '\0';       /* tap again: deselect */
            else
                strlcpy(s_selected_hex, st->contacts[best].hex,
                        sizeof(s_selected_hex));
        } else {
            s_range_idx = (s_range_idx + 1) % N_RANGES;
        }
    }
    was_down = down;
}

/* ------------------------------------------------------------------ */

void radar_tick(void)
{
    static adsb_state_t st;
    adsb_snapshot(&st);

    float scale = (float)PLOT_R / (float)s_ranges_km[s_range_idx];

    handle_touch(&st, scale);

    draw_static_scope();
    draw_sweep();

    bool stale = !st.link_ok ||
                 (esp_timer_get_time() - st.last_ok_us >
                  (int64_t)REFRESH_INTERVAL_S * 4 * 1000000);
    for (int i = 0; i < st.count; i++) {
        bool selected = s_selected_hex[0] &&
                        strcmp(st.contacts[i].hex, s_selected_hex) == 0;
        if (stale && !selected) {
            /* dim stale traffic by drawing label-only ghost */
            int px, py;
            if (contact_screen_pos(&st.contacts[i], scale, &px, &py))
                gfx_circle(px, py, 3, C_GRID);
        } else {
            draw_contact(&st.contacts[i], scale, selected);
        }
    }

    draw_top_bar(&st);
    draw_selected_info(&st);
    draw_bottom_bar(&st);

    display_flush();
}

/* ------------------------------------------------------------------ */
/* boot sequence                                                        */

void radar_boot_begin(void)
{
    gfx_clear(C_BG);
    gfx_circle(CX, CY, BEZEL_R, C_GRID);
    gfx_circle(CX, CY, BEZEL_R - 2, C_GRID_DIM);
    gfx_text_center(CX, 52, "TACSCOPE", C_TEXT, 2);
    gfx_text_center(CX, 72, "AIR SURVEILLANCE", C_TEXT_DIM, 1);
    gfx_text_center(CX, 84, "AN/ESP 2424S012", C_TEXT_DIM, 1);
    s_boot_y = 104;
    display_flush();
}

void radar_boot_line(const char *label, const char *result, bool fail)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%-14s %s", label, result);
    if (s_boot_y > 190) s_boot_y = 104; /* unlikely overflow guard */
    gfx_text(58, s_boot_y, buf, fail ? C_ALERT : C_TEXT, 1);
    s_boot_y += 12;
    display_flush();
}
