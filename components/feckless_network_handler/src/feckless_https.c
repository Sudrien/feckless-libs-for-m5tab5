/*
 * feckless_https.c -- see feckless_https.h. Defeatist's devcert.c (5121,
 * 6006, 8064) and the httpd_ssl_start() half of its remote.c, moved to the
 * launcher's side.
 *
 * SPDX-License-Identifier: MIT
 */
#include "feckless_https.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mdns.h"
#include "nvs.h"

#include "feckless_cert.h"
#include "feckless_redirect.h"

static const char *TAG = "feckless_https";

#define NS          "feckless_https"    /* 14 characters: NVS's limit is 15 */
#define KEY_CAP     (512)
#define CRT_CAP     (1400)
#define NAME_CAP    (40)

/* Held for the program's life once made: the server keeps pointers into
 * them. PSRAM, since nothing here needs internal RAM. */
static char  *s_key, *s_crt;
static size_t s_key_len, s_crt_len;     /* including the NUL, as esp_https_server wants */
static char   s_fp[FECKLESS_HTTPS_FP_SIZE];
static char   s_name[NAME_CAP];
static uint16_t s_port = 443;          /* the last server started: where the redirect points */

/* One lock, made on first use: creating it is the only thing under the
 * critical section, and a certificate takes seconds, which is not. */
static StaticSemaphore_t s_lock_buf;
static SemaphoreHandle_t s_lock;
static portMUX_TYPE      s_mux = portMUX_INITIALIZER_UNLOCKED;

static SemaphoreHandle_t lock(void)
{
    portENTER_CRITICAL(&s_mux);
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    portEXIT_CRITICAL(&s_mux);
    return s_lock;
}

static int hw_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

void feckless_https_set_name(const char *name)
{
    char label[NAME_CAP];
    if (!feckless_dns_label(name, label, sizeof label)) return;
    SemaphoreHandle_t l = lock();
    xSemaphoreTake(l, portMAX_DELAY);
    snprintf(s_name, sizeof s_name, "%s", label);
    xSemaphoreGive(l);
}

/* The name when none was set: the chip's own MAC, which is there whether
 * or not a radio is up. Under the lock. */
static void default_name(void)
{
    if (s_name[0]) return;
    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    snprintf(s_name, sizeof s_name, "feckless-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static bool load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t kl = KEY_CAP, cl = CRT_CAP, nl = sizeof s_name;
    char name[NAME_CAP];
    const bool ok = nvs_get_str(h, "key", s_key, &kl) == ESP_OK &&
                    nvs_get_str(h, "crt", s_crt, &cl) == ESP_OK &&
                    nvs_get_str(h, "name", name, &nl) == ESP_OK && name[0];
    nvs_close(h);
    if (!ok) return false;
    /* The name the certificate was made for, whatever set_name() said
     * since: the mDNS name and the redirect must be the certificate's. */
    snprintf(s_name, sizeof s_name, "%s", name);
    s_key_len = kl;         /* nvs_get_str counts the NUL */
    s_crt_len = cl;
    return true;
}

static bool store(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    /* Both in one commit: a cut between them leaves the old pair, or the
     * new, never a new key with the old certificate (nvs_commit() is the
     * point both become durable together; Defeatist's nvsh version erased
     * the certificate first for want of this). */
    const bool ok = nvs_set_str(h, "key", s_key) == ESP_OK &&
                    nvs_set_str(h, "crt", s_crt) == ESP_OK &&
                    nvs_set_str(h, "name", s_name) == ESP_OK &&
                    nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

static void fingerprint_text(void)
{
    unsigned char fp[32];
    if (feckless_cert_fingerprint(s_crt, fp) != 0) return;
    for (int i = 0; i < 32; i++) {
        snprintf(s_fp + i * 3, sizeof s_fp - (size_t)i * 3, i < 31 ? "%02X:" : "%02X", fp[i]);
    }
    ESP_LOGI(TAG, "SHA-256 %s", s_fp);
}

/* The identity, made or loaded once; under the lock. */
static esp_err_t identity(const char **crt, size_t *crt_len, const char **key, size_t *key_len)
{
    SemaphoreHandle_t l = lock();
    xSemaphoreTake(l, portMAX_DELAY);
    esp_err_t err = ESP_OK;

    if (!s_crt_len) {
        if (!s_key) s_key = heap_caps_calloc(1, KEY_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_crt) s_crt = heap_caps_calloc(1, CRT_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_key || !s_crt) {
            err = ESP_ERR_NO_MEM;
        } else if (!load()) {
            default_name();
            const int64_t t0 = esp_timer_get_time();
            const int ret = feckless_cert_make(s_name, hw_rng, NULL, s_key, KEY_CAP, s_crt, CRT_CAP);
            if (ret == FECKLESS_CERT_ERR_NO_WRITER) {
                ESP_LOGE(TAG, "this Mbed TLS has no X.509 writer (IDF 6: CONFIG_MBEDTLS_X509_CREATE_C=y)");
                err = ESP_ERR_NOT_SUPPORTED;
            } else if (ret != 0) {
                ESP_LOGE(TAG, "could not make a certificate: -0x%04x", (unsigned)-ret);
                err = ESP_FAIL;
            } else {
                s_key_len = strlen(s_key) + 1;
                s_crt_len = strlen(s_crt) + 1;
                ESP_LOGI(TAG, "made a certificate for %s in %lld ms%s", s_name,
                         (long long)((esp_timer_get_time() - t0) / 1000),
                         store() ? "" : " (NOT kept: NVS refused it; a new one next boot)");
            }
        }
        if (err == ESP_OK) fingerprint_text();
    }
    if (err == ESP_OK) {
        if (crt) *crt = s_crt;
        if (crt_len) *crt_len = s_crt_len;
        if (key) *key = s_key;
        if (key_len) *key_len = s_key_len;
    }
    xSemaphoreGive(l);
    return err;
}

/* 8004: the name the certificate carries and the redirect sends a browser
 * to has to be answered by something. mDNS, on the station interface the
 * IDF predefines (CONFIG_MDNS_PREDEF_NETIF_STA); mdns_init() may be
 * called again. The service record is for a browser or a scanner that
 * looks; the hostname is what matters. Failure is logged and not fatal:
 * the server is up, by IP address, and the redirect is the app's to
 * leave off. */
static void mdns_up(const char *name, uint16_t port)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(name);
    if (err == ESP_OK) err = mdns_instance_name_set(name);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS did not start (%d): %s.local will not resolve by itself", (int)err, name);
        return;
    }
    /* Already added by an earlier server on the same port: not an error. */
    (void)mdns_service_add(NULL, "_https", "_tcp", port, NULL, 0);
    ESP_LOGI(TAG, "answering mDNS as %s.local", name);
}

esp_err_t feckless_https_start(httpd_handle_t *out, const httpd_config_t *cfg, uint16_t port)
{
    if (!out || !cfg || !port) return ESP_ERR_INVALID_ARG;

    const char *crt, *key;
    size_t crt_len, key_len;
    ESP_RETURN_ON_ERROR(identity(&crt, &crt_len, &key, &key_len), TAG, "no certificate");

    httpd_ssl_config_t ssl = HTTPD_SSL_CONFIG_DEFAULT();
    ssl.httpd = *cfg;
    ssl.port_secure = port;
    ssl.servercert = (const uint8_t *)crt;
    ssl.servercert_len = crt_len;
    ssl.prvtkey_pem = (const uint8_t *)key;
    ssl.prvtkey_len = key_len;
    const esp_err_t err = httpd_ssl_start(out, &ssl);
    if (err == ESP_OK) {
        char name[NAME_CAP];
        SemaphoreHandle_t l = lock();
        xSemaphoreTake(l, portMAX_DELAY);
        s_port = port;
        snprintf(name, sizeof name, "%s", s_name);
        xSemaphoreGive(l);
        mdns_up(name, port);
    }
    return err;
}

esp_err_t feckless_https_stop(httpd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return httpd_ssl_stop(handle);
}

bool feckless_https_fingerprint(char *out, size_t out_size)
{
    if (!out || out_size < sizeof s_fp) return false;
    SemaphoreHandle_t l = lock();
    xSemaphoreTake(l, portMAX_DELAY);
    const bool have = s_fp[0] != '\0';
    if (have) memcpy(out, s_fp, sizeof s_fp);
    xSemaphoreGive(l);
    return have;
}

/* A request URI: esp_http_server's own limit, CONFIG_HTTPD_MAX_URI_LEN
 * (512, which feckless_abi_network checks), plus "https://", a name and a
 * port. */
#define LOC_CAP (CONFIG_HTTPD_MAX_URI_LEN + 96)

esp_err_t feckless_https_redirect(httpd_req_t *req)
{
    if (!req) return ESP_ERR_INVALID_ARG;

    /* The name the certificate is for, whether or not it has been made:
     * set_name() or the MAC's default, so the redirect and the identity
     * cannot disagree. */
    char name[NAME_CAP];
    SemaphoreHandle_t l = lock();
    xSemaphoreTake(l, portMAX_DELAY);
    default_name();
    snprintf(name, sizeof name, "%s", s_name);
    const uint16_t port = s_port;
    xSemaphoreGive(l);

    char host[80] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) != ESP_OK) host[0] = '\0';

    char loc[LOC_CAP];
    if (!feckless_redirect_target(host, req->uri, name, port, loc, sizeof loc)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no redirect");
    }
    /* 307, not 301: a browser keeps a 301 for good, and this one depends on
     * a name that has to resolve (mDNS); a name that stopped would leave
     * the browser going there without asking this server again. */
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, NULL, 0);
}
