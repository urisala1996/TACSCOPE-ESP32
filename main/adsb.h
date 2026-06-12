#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAX_CONTACTS 24
#define TRAIL_LEN    6

#define ALT_UNKNOWN  (-1)
#define ALT_GROUND   (-2)

typedef struct {
    char   hex[8];        /* ICAO 24-bit address, hex string */
    char   callsign[10];  /* flight id, trimmed; may be empty */
    float  east_km;       /* position relative to HOME, +east  */
    float  north_km;      /*                          +north  */
    int    alt_ft;        /* barometric ft, ALT_UNKNOWN/ALT_GROUND */
    float  gs_kt;         /* ground speed, <0 unknown */
    float  track_deg;     /* true track, <0 unknown   */
    float  trail_e[TRAIL_LEN];
    float  trail_n[TRAIL_LEN];
    uint8_t trail_len;
} contact_t;

typedef struct {
    contact_t contacts[MAX_CONTACTS];
    int       count;          /* contacts stored (closest first) */
    int       total_seen;     /* aircraft reported by the API    */
    bool      link_ok;        /* last fetch succeeded            */
    bool      have_data;      /* at least one successful fetch   */
    int64_t   last_ok_us;     /* esp_timer time of last success  */
} adsb_state_t;

void adsb_start(void);
void adsb_suspend(void);   /* pause the fetch task (used during provisioning) */
/* Copy a consistent snapshot of the shared state for rendering. */
void adsb_snapshot(adsb_state_t *out);
