/* Real-time traffic feed: polls a readsb-style aggregator
 * (<base>/v2/point/<lat>/<lon>/<radius_nm>, e.g. api.adsb.lol) and keeps a
 * shared contact list. The JSON body can be large in busy airspace, so it is
 * parsed as a stream: each element of the "ac" array is captured by
 * brace-matching and parsed individually with cJSON — peak RAM stays bounded
 * no matter how many aircraft the API returns. */
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "cJSON.h"
#include "adsb.h"
#include "wifi.h"
#include "app_config.h"

static const char *TAG = "adsb";

static adsb_state_t s_state;
static SemaphoreHandle_t s_lock;

/* contacts collected during the fetch in progress */
static contact_t s_incoming[MAX_CONTACTS];
static int s_incoming_count;
static int s_incoming_total;

/* ------------------------------------------------------------------ */
/* streaming "ac" array extractor                                      */

#define OBJ_BUF_SZ 2048

typedef struct {
    enum { PS_SEEK_AC, PS_SEEK_BRACKET, PS_BETWEEN, PS_OBJECT, PS_DONE } st;
    int  match;               /* progress through the "ac" key literal  */
    int  depth;
    bool in_str, esc;
    bool overflow;
    int  len;
    char buf[OBJ_BUF_SZ];
} jstream_t;

static const char AC_KEY[] = "\"ac\"";

static void contact_from_json(const char *json);

static void jstream_feed(jstream_t *p, const char *data, int n)
{
    for (int i = 0; i < n; i++) {
        char c = data[i];
        switch (p->st) {
        case PS_SEEK_AC:
            if (c == AC_KEY[p->match]) {
                if (AC_KEY[++p->match] == '\0') p->st = PS_SEEK_BRACKET;
            } else {
                p->match = (c == AC_KEY[0]) ? 1 : 0;
            }
            break;
        case PS_SEEK_BRACKET:
            if (c == '[') p->st = PS_BETWEEN;
            else if (c != ':' && c != ' ' && c != '\t' &&
                     c != '\r' && c != '\n') {
                p->st = PS_SEEK_AC;  /* "ac" wasn't an array key, keep looking */
                p->match = 0;
            }
            break;
        case PS_BETWEEN:
            if (c == '{') {
                p->st = PS_OBJECT;
                p->depth = 1;
                p->len = 0;
                p->overflow = false;
                p->in_str = p->esc = false;
                p->buf[p->len++] = c;
            } else if (c == ']') {
                p->st = PS_DONE;
            }
            break;
        case PS_OBJECT:
            if (p->len < OBJ_BUF_SZ - 1) p->buf[p->len++] = c;
            else p->overflow = true;
            if (p->in_str) {
                if (p->esc) p->esc = false;
                else if (c == '\\') p->esc = true;
                else if (c == '"') p->in_str = false;
            } else if (c == '"') {
                p->in_str = true;
            } else if (c == '{' || c == '[') {
                p->depth++;
            } else if (c == '}' || c == ']') {
                if (--p->depth == 0) {
                    p->buf[p->len] = '\0';
                    if (!p->overflow) contact_from_json(p->buf);
                    p->st = PS_BETWEEN;
                }
            }
            break;
        case PS_DONE:
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* per-aircraft parsing                                                 */

static void latlon_to_km(double lat, double lon, float *east, float *north)
{
    const double km_per_deg_lat = 110.574;
    const double km_per_deg_lon = 111.320 * cos(HOME_LAT * M_PI / 180.0);
    *east  = (float)((lon - HOME_LON) * km_per_deg_lon);
    *north = (float)((lat - HOME_LAT) * km_per_deg_lat);
}

static void contact_from_json(const char *json)
{
    s_incoming_total++;

    cJSON *root = cJSON_Parse(json);
    if (!root) return;

    cJSON *jlat = cJSON_GetObjectItem(root, "lat");
    cJSON *jlon = cJSON_GetObjectItem(root, "lon");
    cJSON *jhex = cJSON_GetObjectItem(root, "hex");
    if (!cJSON_IsNumber(jlat) || !cJSON_IsNumber(jlon) ||
        !cJSON_IsString(jhex)) {
        cJSON_Delete(root);
        return;
    }

    contact_t ct = { 0 };
    ct.alt_ft = ALT_UNKNOWN;
    ct.gs_kt = -1.0f;
    ct.track_deg = -1.0f;

    strlcpy(ct.hex, jhex->valuestring, sizeof(ct.hex));
    latlon_to_km(jlat->valuedouble, jlon->valuedouble,
                 &ct.east_km, &ct.north_km);

    cJSON *j = cJSON_GetObjectItem(root, "flight");
    if (cJSON_IsString(j)) {
        strlcpy(ct.callsign, j->valuestring, sizeof(ct.callsign));
        for (int k = (int)strlen(ct.callsign) - 1;
             k >= 0 && ct.callsign[k] == ' '; k--)
            ct.callsign[k] = '\0';
    }

    j = cJSON_GetObjectItem(root, "alt_baro");
    if (cJSON_IsNumber(j)) ct.alt_ft = (int)j->valuedouble;
    else if (cJSON_IsString(j) && strcmp(j->valuestring, "ground") == 0)
        ct.alt_ft = ALT_GROUND;

    j = cJSON_GetObjectItem(root, "gs");
    if (cJSON_IsNumber(j)) ct.gs_kt = (float)j->valuedouble;

    j = cJSON_GetObjectItem(root, "track");
    if (cJSON_IsNumber(j)) ct.track_deg = (float)j->valuedouble;

    cJSON_Delete(root);

#if !SHOW_GROUND_TARGETS
    if (ct.alt_ft == ALT_GROUND) return;
#endif

    float d2 = ct.east_km * ct.east_km + ct.north_km * ct.north_km;
    if (s_incoming_count < MAX_CONTACTS) {
        s_incoming[s_incoming_count++] = ct;
    } else {
        /* full: replace the farthest contact if this one is closer */
        int worst = 0;
        float worst_d2 = 0;
        for (int i = 0; i < MAX_CONTACTS; i++) {
            float d = s_incoming[i].east_km * s_incoming[i].east_km +
                      s_incoming[i].north_km * s_incoming[i].north_km;
            if (d > worst_d2) { worst_d2 = d; worst = i; }
        }
        if (d2 < worst_d2) s_incoming[worst] = ct;
    }
}

/* ------------------------------------------------------------------ */
/* trail bookkeeping + publish                                          */

static void publish_incoming(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    for (int i = 0; i < s_incoming_count; i++) {
        contact_t *nc = &s_incoming[i];
        for (int k = 0; k < s_state.count; k++) {
            contact_t *oc = &s_state.contacts[k];
            if (strcmp(nc->hex, oc->hex) != 0) continue;
            /* carry the trail over; record previous position if moved */
            memcpy(nc->trail_e, oc->trail_e, sizeof(nc->trail_e));
            memcpy(nc->trail_n, oc->trail_n, sizeof(nc->trail_n));
            nc->trail_len = oc->trail_len;
            float de = oc->east_km - nc->east_km;
            float dn = oc->north_km - nc->north_km;
            if (de * de + dn * dn > 0.05f) {
                if (nc->trail_len < TRAIL_LEN) nc->trail_len++;
                for (int t = nc->trail_len - 1; t > 0; t--) {
                    nc->trail_e[t] = nc->trail_e[t - 1];
                    nc->trail_n[t] = nc->trail_n[t - 1];
                }
                nc->trail_e[0] = oc->east_km;
                nc->trail_n[0] = oc->north_km;
            }
            break;
        }
    }

    memcpy(s_state.contacts, s_incoming,
           s_incoming_count * sizeof(contact_t));
    s_state.count = s_incoming_count;
    s_state.total_seen = s_incoming_total;
    s_state.link_ok = true;
    s_state.have_data = true;
    s_state.last_ok_us = esp_timer_get_time();

    xSemaphoreGive(s_lock);
}

static void mark_link_down(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_state.link_ok = false;
    xSemaphoreGive(s_lock);
}

/* ------------------------------------------------------------------ */
/* fetch task                                                           */

static bool fetch_once(void)
{
    char url[160];
    snprintf(url, sizeof(url), "%s/v2/point/%.4f/%.4f/%d",
             ADSB_BASE_URL, (double)HOME_LAT, (double)HOME_LON,
             (int)FETCH_RADIUS_NM);

    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .buffer_size = 2048,
        .user_agent = "TACSCOPE-ESP32/1.0",
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    bool ok = false;
    do {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGW(TAG, "open failed");
            break;
        }
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP %d", status);
            break;
        }

        s_incoming_count = 0;
        s_incoming_total = 0;
        static jstream_t parser;
        memset(&parser, 0, sizeof(parser));

        char chunk[1024];
        int rd;
        while ((rd = esp_http_client_read(client, chunk, sizeof(chunk))) > 0)
            jstream_feed(&parser, chunk, rd);

        if (rd < 0) {
            ESP_LOGW(TAG, "read error");
            break;
        }
        publish_incoming();
        ESP_LOGI(TAG, "contacts: %d (of %d in range)",
                 s_incoming_count, s_incoming_total);
        ok = true;
    } while (0);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ok;
}

static void adsb_task(void *arg)
{
    int failures = 0;
    for (;;) {
        if (wifi_is_connected()) {
            if (fetch_once()) {
                failures = 0;
            } else {
                mark_link_down();
                failures++;
            }
        } else {
            mark_link_down();
        }
        /* back off a little when the API keeps failing */
        int wait_s = REFRESH_INTERVAL_S;
        if (failures > 3) wait_s = REFRESH_INTERVAL_S * 3;
        vTaskDelay(pdMS_TO_TICKS(wait_s * 1000));
    }
}

void adsb_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(adsb_task, "adsb", 12288, NULL, 4, NULL);
}

void adsb_snapshot(adsb_state_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_state;
    xSemaphoreGive(s_lock);
}
