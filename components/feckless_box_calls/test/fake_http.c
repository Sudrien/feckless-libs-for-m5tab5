/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0073: esp_http_client as box_http.c sees it, over a socketpair
 * per client -- the host's sockets, so the stop's shutdown is real -- and
 * esp_timer's create/start/stop recorded. What a client was given is kept
 * where the test can look at it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_timer.h"

void box_http_reap(void *arg);
extern bool fake_stopping;

struct esp_http_client {
    esp_http_client_config_t cfg;           /* as given: pointers into the launcher */
    char *url;
    int fd[2];
    const char *post;
    int post_len;
    char req_key[64], req_val[256];
    int written, read_at;
    bool opened;
};

esp_http_client_handle_t fake_http_last;    /* the last init's */
int fake_http_live;                         /* clients not cleaned up */
bool fake_http_reap_in_read;                /* the next read: the stop comes while it waits */
int fake_http_read_got;                     /* what that wait's recv() returned */
const char *fake_http_body = "hello world";

esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    if (config->url && strstr(config->url, "no-init")) return NULL;
    struct esp_http_client *c = calloc(1, sizeof *c);
    c->cfg = *config;
    c->url = config->url ? strdup(config->url) : NULL;
    socketpair(AF_UNIX, SOCK_STREAM, 0, c->fd);
    fake_http_live++;
    return fake_http_last = c;
}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t c)
{
    close(c->fd[0]);
    close(c->fd[1]);
    free(c->url);
    free(c);
    fake_http_live--;
    return ESP_OK;
}
esp_err_t esp_http_client_set_url(esp_http_client_handle_t c, const char *url)
{
    free(c->url);
    c->url = strdup(url);
    return ESP_OK;
}
esp_err_t esp_http_client_get_url(esp_http_client_handle_t c, char *url, const int len)
{
    snprintf(url, (size_t)len, "%s", c->url ? c->url : "");
    return ESP_OK;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t c, const char *d, int len)
{
    c->post = d;
    c->post_len = len;
    return ESP_OK;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t c, const char *k, const char *v)
{
    snprintf(c->req_key, sizeof c->req_key, "%s", k);
    snprintf(c->req_val, sizeof c->req_val, "%s", v ? v : "");
    return ESP_OK;
}
esp_err_t esp_http_client_get_header(esp_http_client_handle_t c, const char *k, char **v)
{
    *v = !strcasecmp(k, c->req_key) ? c->req_val : NULL;
    return ESP_OK;
}
static char s_long[4096];
esp_err_t esp_http_client_get_response_header(esp_http_client_handle_t c, const char *k, char **v)
{
    (void)c;
    *v = NULL;
    if (!strcasecmp(k, "icy-metaint")) *v = "16000";
    else if (!strcasecmp(k, "x-long")) { memset(s_long, 'z', sizeof s_long - 1); *v = s_long; }
    else return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}
esp_err_t esp_http_client_set_method(esp_http_client_handle_t c, esp_http_client_method_t m) { c->cfg.method = m; return ESP_OK; }
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t c, int t) { c->cfg.timeout_ms = t; return ESP_OK; }
esp_err_t esp_http_client_set_redirection(esp_http_client_handle_t c) { (void)c; return ESP_OK; }
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int wl)
{
    (void)wl;
    if (c->url && strstr(c->url, "refused")) return ESP_FAIL;
    c->opened = true;
    c->read_at = 0;
    return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t c, const char *b, int len) { (void)b; c->written += len; return len; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t c)
{
    return c->opened ? (int64_t)strlen(fake_http_body) : ESP_FAIL;
}
int esp_http_client_read(esp_http_client_handle_t c, char *b, int len)
{
    if (fake_http_reap_in_read) {           /* the timer fires while this waits */
        fake_http_reap_in_read = false;
        fake_stopping = true;
        box_http_reap(NULL);
        char x;
        fake_http_read_got = (int)recv(c->fd[0], &x, 1, 0);   /* shut down: 0 at once, not forever */
        return -1;
    }
    const int left = (int)strlen(fake_http_body) - c->read_at;
    const int n = len < left ? len : left;
    memcpy(b, fake_http_body + c->read_at, (size_t)n);
    c->read_at += n;
    return n;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t c) { return c->read_at == (int)strlen(fake_http_body); }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { return c->opened ? 200 : 0; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t c) { return c->opened ? 0x100000005LL : -1; }
esp_err_t esp_http_client_perform(esp_http_client_handle_t c) { return esp_http_client_open(c, 0); }
esp_err_t esp_http_client_close(esp_http_client_handle_t c) { c->opened = false; return ESP_OK; }
int esp_http_client_get_socket(esp_http_client_handle_t c) { return c->fd[0]; }

/* esp_timer, recorded */
struct fake_esp_timer { esp_timer_cb_t cb; };
int fake_timer_running, fake_timer_made;
esp_err_t esp_timer_create(const esp_timer_create_args_t *a, esp_timer_handle_t *out)
{
    *out = calloc(1, sizeof **out);
    (*out)->cb = a->callback;
    fake_timer_made++;
    return ESP_OK;
}
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t us) { (void)t; (void)us; fake_timer_running = 1; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t t) { (void)t; fake_timer_running = 0; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t t) { free(t); fake_timer_made--; return ESP_OK; }
