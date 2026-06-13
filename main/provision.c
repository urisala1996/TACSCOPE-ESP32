/* SoftAP captive-portal Wi-Fi provisioning, styled to match the scope.
 *
 *   - wifi_start_ap() puts the radio in open-AP mode at 192.168.4.1
 *   - a tiny UDP DNS server answers every query with the AP address so phones
 *     auto-detect the captive portal
 *   - an HTTP server serves a phosphor-green setup form and a catch-all that
 *     302-redirects probe requests to it
 *   - POST /save persists the credentials and the device reboots into STA mode
 *
 * The round display shows join instructions and a live status line. */
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "gfx.h"
#include "theme.h"
#include "display.h"
#include "wifi.h"
#include "adsb.h"
#include "config.h"
#include "provision.h"
#include "app_config.h"

static const char *TAG = "provision";

static httpd_handle_t  s_http;
static volatile bool   s_saved;
static volatile int    s_clients;     /* stations joined to the SoftAP */

/* --------------------------------------------------------------- portal -- */

/* The page is sent in three chunks: a static head (with CSS, which contains '%'
 * characters that must not reach snprintf), a dynamic middle holding the input
 * fields pre-filled with the current values, and a static tail. */
static const char PAGE_HEAD[] =
"<!DOCTYPE html><html><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>TACSCOPE SETUP</title><style>"
"body{background:#000a02;color:#23c850;font-family:monospace;margin:0;padding:24px}"
"h1{color:#3effa0;letter-spacing:3px;font-size:19px;border-bottom:1px solid #0a4a1a;padding-bottom:8px}"
"label{display:block;margin:18px 0 4px;font-size:13px;color:#1a8a3a}"
"input{width:100%;box-sizing:border-box;background:#02160a;border:1px solid #0e5a22;"
"color:#3effa0;padding:11px;font-family:monospace;font-size:15px}"
"button{margin-top:26px;width:100%;background:#0e5a22;color:#bfffd0;border:none;"
"padding:14px;font-family:monospace;font-size:16px;letter-spacing:2px}"
".f{color:#0e6a2a;font-size:11px;margin-top:22px;text-align:center;letter-spacing:1px}"
".h{color:#0e6a2a;font-size:11px;margin:4px 0 0}"
"</style></head><body>"
"<h1>TACSCOPE // LINK SETUP</h1>"
"<form method=POST action=/save>";

static const char PAGE_TAIL[] =
"<button type=submit>SAVE &amp; RESTART</button>"
"</form>"
"<div class=f>AN/ESP 2424S012 &nbsp;&middot;&nbsp; AIR SURVEILLANCE</div>"
"</body></html>";

static const char SAVED_PAGE[] =
"<!DOCTYPE html><html><head><meta charset=utf-8>"
"<meta name=viewport content=\"width=device-width,initial-scale=1\">"
"<title>TACSCOPE</title><style>"
"body{background:#000a02;color:#3effa0;font-family:monospace;text-align:center;padding:60px 24px}"
"h1{letter-spacing:3px}p{color:#1a8a3a}</style></head><body>"
"<h1>LINK SAVED</h1><p>Rebooting and connecting&hellip;</p></body></html>";

static int hexv(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

/* In-place url-decode of an application/x-www-form-urlencoded token. */
static void url_decode(char *s)
{
    char *o = s;
    for (char *i = s; *i; i++) {
        if (*i == '+') {
            *o++ = ' ';
        } else if (*i == '%' && i[1] && i[2]) {
            int hi = hexv(i[1]), lo = hexv(i[2]);
            if (hi >= 0 && lo >= 0) { *o++ = (char)(hi << 4 | lo); i += 2; }
            else *o++ = *i;
        } else {
            *o++ = *i;
        }
    }
    *o = '\0';
}

/* Copy the value of form field `key` out of `body` into `out`. */
static void form_field(const char *body, const char *key,
                       char *out, size_t out_sz)
{
    out[0] = '\0';
    size_t klen = strlen(key);
    for (const char *p = body; p && *p; ) {
        const char *amp = strchr(p, '&');
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t vlen = amp ? (size_t)(amp - v) : strlen(v);
            if (vlen >= out_sz) vlen = out_sz - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            url_decode(out);
            return;
        }
        if (!amp) break;
        p = amp + 1;
    }
}

/* Escape a string for use inside an HTML double-quoted attribute value. */
static void html_escape(const char *in, char *out, size_t out_sz)
{
    size_t o = 0;
    for (const char *p = in; *p && o + 7 < out_sz; p++) {
        const char *rep = NULL;
        switch (*p) {
        case '&': rep = "&amp;";  break;
        case '"': rep = "&quot;"; break;
        case '<': rep = "&lt;";   break;
        case '>': rep = "&gt;";   break;
        default:  out[o++] = *p;  continue;
        }
        size_t rl = strlen(rep);
        memcpy(out + o, rep, rl);
        o += rl;
    }
    out[o] = '\0';
}

static esp_err_t root_get(httpd_req_t *r)
{
    static char ssid_e[208], pass_e[400], lat_s[320], lon_s[320], mid[1792];
    html_escape(config_wifi_ssid(), ssid_e, sizeof(ssid_e));
    html_escape(config_wifi_pass(), pass_e, sizeof(pass_e));
    /* lat/lon are bounded to valid ranges, so these never grow long */
    snprintf(lat_s, sizeof(lat_s), "%.6f", config_home_lat());
    snprintf(lon_s, sizeof(lon_s), "%.6f", config_home_lon());

    snprintf(mid, sizeof(mid),
        "<label>NETWORK SSID</label>"
        "<input name=ssid maxlength=32 autocomplete=off value=\"%s\">"
        "<label>PASSPHRASE</label>"
        "<input name=pass type=password maxlength=63 autocomplete=off value=\"%s\">"
        "<p class=h>Leave Wi-Fi fields as-is to only change position.</p>"
        "<label>LATITUDE</label>"
        "<input name=lat maxlength=12 inputmode=decimal value=\"%s\">"
        "<label>LONGITUDE</label>"
        "<input name=lon maxlength=12 inputmode=decimal value=\"%s\">",
        ssid_e, pass_e, lat_s, lon_s);

    httpd_resp_set_type(r, "text/html");
    httpd_resp_send_chunk(r, PAGE_HEAD, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(r, mid, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(r, PAGE_TAIL, HTTPD_RESP_USE_STRLEN);
    return httpd_resp_send_chunk(r, NULL, 0);
}

static esp_err_t redirect_get(httpd_req_t *r)
{
    httpd_resp_set_status(r, "302 Found");
    httpd_resp_set_hdr(r, "Location", "http://192.168.4.1/");
    return httpd_resp_send(r, NULL, 0);
}

static esp_err_t save_post(httpd_req_t *r)
{
    char buf[256];
    int total = r->content_len;
    if (total > (int)sizeof(buf) - 1) total = sizeof(buf) - 1;
    int got = 0;
    while (got < total) {
        int rd = httpd_req_recv(r, buf + got, total - got);
        if (rd <= 0) return ESP_FAIL;
        got += rd;
    }
    buf[got] = '\0';

    char ssid[33], pass[64], lats[24], lons[24];
    form_field(buf, "ssid", ssid, sizeof(ssid));
    form_field(buf, "pass", pass, sizeof(pass));
    form_field(buf, "lat", lats, sizeof(lats));
    form_field(buf, "lon", lons, sizeof(lons));

    bool changed = false;
    if (ssid[0]) {
        config_set_wifi(ssid, pass);
        changed = true;
    }

    /* both coordinates must parse and be in range to update the position */
    if (lats[0] && lons[0]) {
        char *le, *oe;
        double lat = strtod(lats, &le);
        double lon = strtod(lons, &oe);
        if (le != lats && oe != lons &&
            lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0) {
            config_save_location(lat, lon);
            changed = true;
        }
    }

    httpd_resp_set_type(r, "text/html");
    if (changed) {
        httpd_resp_send(r, SAVED_PAGE, HTTPD_RESP_USE_STRLEN);
        s_saved = true;             /* picked up by the render loop -> reboot */
    } else {
        redirect_get(r);
    }
    return ESP_OK;
}

static void http_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    if (httpd_start(&s_http, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = root_get };
    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t any  = { .uri = "/*", .method = HTTP_GET, .handler = redirect_get };
    httpd_register_uri_handler(s_http, &root);
    httpd_register_uri_handler(s_http, &save);
    httpd_register_uri_handler(s_http, &any);
}

/* --------------------------------------------------------------- DNS ----- */
/* Answer every A query with 192.168.4.1 so any hostname opens the portal. */

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) vTaskDelete(NULL);

    struct sockaddr_in sa = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        close(sock);
        vTaskDelete(NULL);
    }

    /* fixed trailer: name pointer to 0x0C, type A, class IN, TTL 60, 4 bytes */
    static const uint8_t ans[] = {
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3C,
        0x00, 0x04, 192, 168, 4, 1
    };
    uint8_t buf[512];
    for (;;) {
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0,
                           (struct sockaddr *)&src, &sl);
        if (len < 12) continue;
        buf[2] = 0x81; buf[3] = 0x80;     /* response, recursion available */
        buf[6] = 0x00; buf[7] = 0x01;     /* answer count = 1              */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;  /* no NS/AR records      */
        if (len + (int)sizeof(ans) <= (int)sizeof(buf)) {
            memcpy(buf + len, ans, sizeof(ans));
            sendto(sock, buf, len + sizeof(ans), 0,
                   (struct sockaddr *)&src, sl);
        }
    }
}

/* --------------------------------------------------------- AP client cnt -- */

static void on_ap_event(void *arg, esp_event_base_t base,
                        int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED)         s_clients++;
    else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_clients > 0) s_clients--;
}

/* --------------------------------------------------------------- screen -- */

static void draw_frame(int frame)
{
    gfx_clear(C_BG);
    gfx_circle(CX, CY, BEZEL_R, C_GRID);
    gfx_circle(CX, CY, BEZEL_R - 2, C_GRID_DIM);

    /* slow sweep tick around the bezel, just for life */
    float a = (frame % 60) / 60.0f * 2.0f * 3.14159265f;
    gfx_line(CX, CY,
             CX + (int)(sinf(a) * (BEZEL_R - 6)),
             CY - (int)(cosf(a) * (BEZEL_R - 6)), C_GRID_DIM);

    gfx_text_center(CX, 40, "TACSCOPE", C_TEXT, 2);
    gfx_text_center(CX, 60, "LINK SETUP", C_TEXT_DIM, 1);

    gfx_text_center(CX, 92,  "JOIN WIFI", C_TEXT_DIM, 1);
    gfx_text_center(CX, 104, PROV_AP_SSID, C_TEXT, 1);
    gfx_text_center(CX, 124, "BROWSE TO", C_TEXT_DIM, 1);
    gfx_text_center(CX, 136, "192.168.4.1", C_TEXT, 1);

    if (s_clients > 0) {
        gfx_text_center(CX, 170, "CLIENT LINKED", C_BLIP, 1);
    } else if (frame & 1) {               /* blink while waiting */
        gfx_text_center(CX, 170, "AWAITING LINK", C_TEXT_DIM, 1);
    }
    display_flush();
}

static void draw_saved(void)
{
    gfx_clear(C_BG);
    gfx_circle(CX, CY, BEZEL_R, C_GRID);
    gfx_circle(CX, CY, BEZEL_R - 2, C_GRID_DIM);
    gfx_text_center(CX, 80, "LINK SAVED", C_TEXT, 2);
    gfx_text_center(CX, 110, config_wifi_ssid(), C_TEXT_DIM, 1);
    gfx_text_center(CX, 140, "REBOOTING", C_SEL, 1);
    display_flush();
}

/* --------------------------------------------------------------- entry --- */

void provision_run(void)
{
    ESP_LOGI(TAG, "entering provisioning mode");
    adsb_suspend();                       /* free CPU; harmless if not started */

    wifi_start_ap(PROV_AP_SSID);
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        on_ap_event, NULL, NULL);
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);
    http_start();

    for (int frame = 0; !s_saved; frame++) {
        draw_frame(frame);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    draw_saved();
    vTaskDelay(pdMS_TO_TICKS(1800));
    esp_restart();
}
