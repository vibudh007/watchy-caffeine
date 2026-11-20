#include "portal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "portal_dns.h"
#include "branding.h"

#define WATCH_ADDRESS "192.168.4.1"
#define WATCH_ORIGIN "http://" WATCH_ADDRESS
#define CAPTIVE_QUERY "captive=1"

static char ssid[sizeof WATCH_WIFI_PREFIX + 4], password[9], token[17];
static QueueHandle_t requests, results;
static httpd_handle_t server;
static esp_netif_t *netif;
static bool loop_owned, wifi_init, wifi_started;
static bool discovery_configured;
static int64_t deadline, response_deadline;
typedef struct {
    enum portal_sync_status status;
    int16_t offset_min;
    uint32_t seconds;
    int32_t error;
} clock_result_t;
static clock_result_t http_result;
static bool request_sent;
static QueueHandle_t response_sent;
#ifdef WATCHY_BENCH_DEMO
static bool discovery_logged;
#endif

static bool header(httpd_req_t *request, const char *name, const char *expected)
{
    char value[48];
    return httpd_req_get_hdr_value_str(request, name, value, sizeof value) == ESP_OK && !strcmp(value, expected);
}

static bool local_host(httpd_req_t *request)
{
    return header(request, "Host", WATCH_ADDRESS) || header(request, "Host", WATCH_ADDRESS ":80");
}

static esp_err_t index_page(httpd_req_t *request)
{
    char query[sizeof CAPTIVE_QUERY];
    bool captive = httpd_req_get_url_query_str(request, query, sizeof query) == ESP_OK &&
                   !strcmp(query, CAPTIVE_QUERY);
    static const char page[] =
        "<!doctype html><html lang=en><meta charset=utf-8>"
        "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
        "<title>" WATCH_PRODUCT_NAME " time setup</title><link rel=icon href=data:,>"
        "<style>body{font:1.1rem system-ui;margin:2rem}"
        "button{font:inherit;padding:.65rem}</style><h1>Set " WATCH_PRODUCT_NAME " time</h1>"
        "<p id=status role=status>Sending this device's time and time zone…</p>"
        "<button id=retry>Try again</button>"
        "<noscript>Enable JavaScript or set the time from the watch's Clock menu.</noscript>"
        "<script>\n"
        "var statusEl = document.getElementById('status');\n"
        "var retry = document.getElementById('retry');\n"
        "function finish(message) {\n"
        "  statusEl.textContent = message;\n"
        "  retry.disabled = false;\n"
        "}\n"
        "function connectionLost() {\n"
        "  finish('Connection lost. Stay on the watch network and try again.');\n"
        "}\n"
        "function sync() {\n"
        "  retry.disabled = true;\n"
        "  statusEl.textContent = 'Setting the watch clock…';\n"
        "  try {\n"
        "    var request = new XMLHttpRequest();\n"
        "    request.open('POST', '/time', true);\n"
        "    request.timeout = 5000;\n"
        "    request.setRequestHeader('Content-Type', 'application/json');\n"
        "    request.setRequestHeader('X-Watchy-Token', '%s');\n"
        "    request.setRequestHeader('X-Watchy-Entry', '%s');\n"
        "    request.onload = function () { finish(request.responseText); };\n"
        "    request.onerror = request.ontimeout = connectionLost;\n"
        "    var now = new Date();\n"
        "    request.send(JSON.stringify({unixMs: now.getTime(), offsetMin: -now.getTimezoneOffset()}));\n"
        "  } catch (error) {\n"
        "    connectionLost();\n"
        "  }\n"
        "}\n"
        "retry.onclick = sync;\n"
        "sync();\n"
        "</script></html>";
    char body[sizeof page + sizeof token + sizeof "captive"];
    int size = snprintf(body, sizeof body, page, token, captive ? "captive" : "manual");
    if (size < 0 || size >= sizeof body) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Page unavailable");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body, size);
}

static esp_err_t discovery_get(httpd_req_t *request)
{
    if (esp_timer_get_time() >= deadline) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Setup expired");
    }
    if (!local_host(request)) {
        httpd_resp_set_status(request, "302 Found");
        httpd_resp_set_hdr(request, "Location", WATCH_ORIGIN "/?" CAPTIVE_QUERY);
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
#ifdef WATCHY_BENCH_DEMO
        if (!discovery_logged) {
            puts("WATCHY_DEMO_CAPTIVE_REDIRECT");
            discovery_logged = true;
        }
#endif
        return httpd_resp_sendstr(request, request->method == HTTP_HEAD ? "" : "Open " WATCH_PRODUCT_NAME " time setup");
    }
    if (request->uri[0] != '/' || (request->uri[1] && request->uri[1] != '?')) {
        return httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "Page not found");
    }
    return request->method == HTTP_HEAD ? httpd_resp_sendstr(request, "") : index_page(request);
}

static esp_err_t time_post(httpd_req_t *request)
{
    if (esp_timer_get_time() >= deadline || !local_host(request) ||
        !header(request, "Origin", WATCH_ORIGIN) || !header(request, "X-Watchy-Token", token) ||
        !header(request, "Content-Type", "application/json")) {
        return httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "Invalid or expired session");
    }
    char body[WATCH_SYNC_BODY_MAX];
    if (!request->content_len || request->content_len > sizeof body) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid clock data");
    }
    unsigned got = 0;
    while (got < request->content_len && esp_timer_get_time() < deadline) {
        int count = httpd_req_recv(request, body + got, request->content_len - got);
        if (count <= 0) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Clock data incomplete");
        }
        got += (unsigned)count;
    }
    watch_time_sync_t sync;
    if (got != request->content_len || esp_timer_get_time() >= deadline || !watch_sync_parse(body, got, &sync)) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid clock data");
    }
    if (!request_sent) {
        if (xQueueSend(requests, &sync, 0) != pdPASS) {
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Clock busy");
        }
        request_sent = true;
#ifdef WATCHY_BENCH_DEMO
        printf("WATCHY_DEMO_SYNC_ENTRY %s\n",
               header(request, "X-Watchy-Entry", "captive") && discovery_logged ? "captive" : "manual");
#endif
    }
    if (http_result.status == PORTAL_SYNC_WAITING &&
        xQueueReceive(results, &http_result, pdMS_TO_TICKS(2000)) != pdPASS) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Clock still busy");
    }
    if (http_result.status == PORTAL_SYNC_OFFSET_FAILED) {
        request_sent = false;
        http_result = (clock_result_t){0};
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not save the UTC offset. Try again.");
    }
    char message[160];
    int offset = http_result.offset_min;
    unsigned minutes = (unsigned)(offset < 0 ? -offset : offset);
    int size = snprintf(message, sizeof message, "%s (UTC%c%02u:%02u).",
                        http_result.status == PORTAL_SYNC_BACKUP ? "Time set using the backup clock" : "Time set",
                        offset < 0 ? '-' : '+', minutes / 60, minutes % 60);
    if (http_result.status == PORTAL_SYNC_EXTERNAL && http_result.seconds && size > 0 && size < sizeof message) {
        snprintf(message + size, sizeof message - size,
                 " RTC change versus device: %+" PRId32 " s over %" PRIu32 " s since the previous sync.",
                 http_result.error, http_result.seconds);
    }
    httpd_resp_set_type(request, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t sent = httpd_resp_sendstr(request, message);
    uint8_t done = 1;
    if (sent == ESP_OK) {
        xQueueOverwrite(response_sent, &done);
    }
    return sent;
}

static bool start_wifi(void)
{
    bootloader_random_enable();
    uint32_t random_ssid = esp_random();
    uint32_t random_pass = esp_random();
    uint32_t random_token[2];
    esp_fill_random(random_token, sizeof random_token);
    bootloader_random_disable();
    snprintf(ssid, sizeof ssid, WATCH_WIFI_PREFIX "%04" PRIX32, random_ssid & 0xffff);
    snprintf(password, sizeof password, "%08" PRIX32, random_pass);
    snprintf(token, sizeof token, "%08" PRIx32 "%08" PRIx32, random_token[0], random_token[1]);

    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) {
        return false;
    }
    loop_owned = true;
    netif = esp_netif_create_default_wifi_ap();
    if (!netif) {
        return false;
    }
    esp_netif_ip_info_t ip;
    uint8_t offer_dns = 1;
    if (esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
        esp_netif_dns_info_t dns = {.ip = {.u_addr.ip4.addr = ip.ip.addr, .type = ESP_IPADDR_TYPE_V4}};
        discovery_configured = esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns) == ESP_OK &&
            esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER,
                                   &offer_dns, sizeof offer_dns) == ESP_OK;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    if (esp_wifi_init(&init) != ESP_OK) {
        return false;
    }
    wifi_init = true;
    wifi_config_t config = {.ap = {.channel = 6, .max_connection = 1, .authmode = WIFI_AUTH_WPA2_PSK}};
    memcpy(config.ap.ssid, ssid, strlen(ssid));
    memcpy(config.ap.password, password, strlen(password));
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK || esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &config) != ESP_OK || esp_wifi_start() != ESP_OK) {
        return false;
    }
    wifi_started = true;
    return true;
}

bool portal_start(void)
{
    if (server) {
        return false;
    }
    request_sent = false;
    discovery_configured = false;
    response_deadline = 0;
#ifdef WATCHY_BENCH_DEMO
    discovery_logged = false;
#endif
    http_result = (clock_result_t){0};
    deadline = esp_timer_get_time() + INT64_C(120000000);
    requests = xQueueCreate(1, sizeof(watch_time_sync_t));
    results = xQueueCreate(1, sizeof(clock_result_t));
    response_sent = xQueueCreate(1, sizeof(uint8_t));
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size += 1024; // Allow for the formatted setup page on the HTTP task stack.
    config.max_open_sockets = 3;
    config.recv_wait_timeout = 1;
    config.send_wait_timeout = 1;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    const httpd_uri_t get = {.uri = "/*", .method = HTTP_GET, .handler = discovery_get};
    const httpd_uri_t head = {.uri = "/*", .method = HTTP_HEAD, .handler = discovery_get};
    const httpd_uri_t post = {.uri = "/time", .method = HTTP_POST, .handler = time_post};
    if (!requests || !results || !response_sent || !start_wifi() || httpd_start(&server, &config) != ESP_OK ||
        httpd_register_uri_handler(server, &get) != ESP_OK || httpd_register_uri_handler(server, &head) != ESP_OK ||
        httpd_register_uri_handler(server, &post) != ESP_OK) {
        portal_stop();
        return false;
    }
    if (discovery_configured) {
        discovery_configured = portal_dns_start(WATCH_ADDRESS);
    }
#ifdef WATCHY_BENCH_DEMO
    printf("WATCHY_DEMO_CAPTIVE_DNS %d\n", discovery_configured);
#endif
    return true;
}

bool portal_request(watch_time_sync_t *sync)
{
    return requests && xQueueReceive(requests, sync, 0) == pdPASS;
}

void portal_clock_result(enum portal_sync_status status, int16_t offset_min,
                         uint32_t observation_seconds, int32_t rtc_error)
{
    clock_result_t result = {
        .status = status, .offset_min = offset_min, .seconds = observation_seconds, .error = rtc_error,
    };
    xQueueOverwrite(results, &result);
}

bool portal_finished(void)
{
    portal_dns_poll();
    uint8_t sent;
    int64_t now = esp_timer_get_time();
    if (xQueueReceive(response_sent, &sent, 0) == pdPASS && !response_deadline) {
        response_deadline = now + INT64_C(600000);
    }
    return now >= deadline || (response_deadline && now >= response_deadline);
}

static bool stop_server(void)
{
    if (server) {
        if (httpd_stop(server) != ESP_OK) {
            return false;
        }
        server = NULL;
    }
    return true;
}

void portal_stop(void)
{
    portal_dns_stop();
    bool stopped = stop_server();
    if (wifi_started) {
        esp_wifi_stop();
        wifi_started = false;
    }
    if (wifi_init) {
        esp_wifi_deinit();
        wifi_init = false;
    }
    if (!stopped && !stop_server()) {
        // The HTTP task can still reference these objects until the next reset.
        return;
    }
    if (netif) {
        esp_netif_destroy_default_wifi(netif);
        netif = NULL;
    }
    if (loop_owned) {
        esp_event_loop_delete_default();
        loop_owned = false;
    }
    if (requests) vQueueDelete(requests);
    if (results) vQueueDelete(results);
    if (response_sent) vQueueDelete(response_sent);
    requests = results = response_sent = NULL;
    memset(token, 0, sizeof token);
    memset(password, 0, sizeof password);
    memset(ssid, 0, sizeof ssid);
}

bool portal_frame(uint8_t frame[WATCH_FRAME_SIZE])
{
    return watch_join_frame(frame, ssid, password);
}
