/* SPDX-License-Identifier: MIT
 *
 * The radio's control for a boxed app (u-feckless-0083): the station's
 * scan, join and saved networks, the switch, and the app's network hooks,
 * so Wi-Fi and NTP work in a box as they do in a trusted build.
 *
 * u-feckless-0084: and the access point -- wifi_ap_begin, its WPA2 form
 * wifi_ap_begin_secure, wifi_ap_end, wifi_ap_clients, wifi_ap_ip -- for an
 * app's hotspot. The AP's netif brings its DHCP server, so nothing about
 * the DHCP server need be answered. wifi_ap_netif() is the app's, in
 * feckless_box_libc: the box's own number for "WIFI_AP_DEF"
 * (u-feckless-0076), never a host pointer. The DHCP server's options and
 * the DNS setters (the setup portal's captive DNS) stay unanswered.
 *
 * THE HOOKS
 * feckless_net_set_hooks() takes the app's policy as functions: whether
 * the radio is wanted, whether NTP is, whether its portal holds the
 * radio, and three notes (heap_note, online, ntp_reply). The launcher's
 * network calls them from its own tasks -- the worker, the event loop,
 * lwIP's SNTP callback -- where a box can never run app code, and where
 * waiting for the app's callback loop could deadlock: an app thread that
 * holds a lock of its own and calls wifi_apply_settings(), whose
 * wifi_enabled() then waits for the loop, which needs that lock; or the
 * SNTP callback on lwIP's thread waiting for an app that is in a socket
 * call, waiting for lwIP's thread. So nothing here waits for the app:
 *
 * - The three yes/no hooks are ANSWERS, worked out by the app on its own
 *   thread. feckless_box_libc's box_wifi.c is the app's
 *   feckless_net_set_hooks(), wifi_apply_settings(), wifi_request_apply()
 *   and wifi_request_restart(): each calls the app's hooks there, in the
 *   app, and hands the answers to feckless_box_net_hooks() or
 *   feckless_box_wifi_apply() with the request. The launcher's hooks are
 *   this file's, and read the answers last given. An app that changes a
 *   setting then asks for it to be applied -- as the network's own README
 *   asks -- is always read fresh; Defeatist also applies at every track
 *   boundary.
 * - heap_note, online and ntp_reply are NOTES, queued for the callback
 *   loop (u-feckless-0075) and delivered in order by a short task of the
 *   launcher's; the asker goes on at once. ntp_reply's answer is taken as
 *   yes: the system clock has moved by then anyway, and the app's own
 *   gate still runs (it decides what the app stores); only
 *   wifi_ntp_synced() can say synced for a reply the app found implausible.
 * - portal_stop is a note too, sent only while the app said its portal
 *   runs -- which in a box it cannot yet.
 *
 * usb_register_class is never the app's: the launcher's, which
 * feckless_net_set_hooks() keeps when given NULL. At the app's end the
 * hooks the launcher had before the app's first are put back, and
 * nothing is queued for it any more; the radio stays as the app left it.
 *
 * THE CALLS
 * - wifi_scan_list (at most SCAN_MAX networks) and wifi_join are unlocked
 *   and may wait: a scan about four seconds, behind a join in progress up
 *   to thirty, and a join its timeout (at most JOIN_MAX_MS). Neither can
 *   be cut short by the app's stop; the stop waits for them.
 * - wifi_apply_settings (which may power the radio up, and with it
 *   ESP-Hosted), wifi_psk and wifistore_save (NVS) run on the flash task's
 *   internal stack (u-feckless-0082).
 * - esp_wifi_get_mac copies the six bytes out.
 *
 * In the network section; built where feckless_net.h and wifi.h are (the
 * launcher links feckless_network_handler).
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("feckless_net.h") && __has_include("wifi.h") && !defined(FECKLESS_BOX_NO_NET)

#include <stdlib.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "feckless_net.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wifi.h"
#include "wifistore.h"

#include "feckless_umode_box.h"
#include "feckless_umode_load.h"

static const char *TAG = "box_wifi";

#define SCAN_MAX     64                     /* networks a scan copies out */
#define JOIN_MAX_MS  60000
#define SSID_MAX     32
#define SECRET_MAX   64
#define WHY_MAX      48
#define NOTE_MAX     32                     /* a heap_note's text */
#define NOTES        8                      /* queued at once; more are dropped */
#define NOTE_STACK   3072
#define LOOP_STACK   (12 * 1024)            /* box_httpd.c's and box_fetch.c's */

/* The answers, as feckless_box_libc's box_wifi.c packs them. */
#define ANS_WIFI     1u
#define ANS_NTP      2u
#define ANS_PORTAL   4u

/* The app's three notes and portal_stop, by their box addresses. */
static uint32_t s_heap_note, s_online, s_ntp_reply, s_portal_stop;
static uint32_t s_answers = ANS_WIFI;      /* feckless_net.h's defaults */
static uint32_t s_gen;                      /* bumped at each app's end */
static uint32_t s_text;                     /* NOTE_MAX bytes in the box for heap_note */
static bool s_saved;                        /* s_launcher holds the launcher's hooks */
static feckless_net_hooks_t s_launcher;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* ---- the notes ------------------------------------------------------- */

typedef enum { NOTE_HEAP, NOTE_ONLINE, NOTE_NTP, NOTE_PORTAL_STOP } note_kind_t;
typedef struct {
    note_kind_t kind;
    uint32_t gen;
    uint32_t a[4];
    char text[NOTE_MAX];
} note_t;

static QueueHandle_t s_notes;

static void note_task(void *arg)
{
    (void)arg;
    note_t n;
    /* portMAX_DELAY: never false on the chip (INCLUDE_vTaskSuspend) */
    while (xQueueReceive(s_notes, &n, portMAX_DELAY) == pdTRUE) {
        portENTER_CRITICAL(&s_mux);
        const bool live = n.gen == s_gen;
        uint32_t fn = 0;
        switch (n.kind) {
        case NOTE_HEAP:        fn = s_heap_note; break;
        case NOTE_ONLINE:      fn = s_online; break;
        case NOTE_NTP:         fn = s_ntp_reply; break;
        case NOTE_PORTAL_STOP: fn = s_portal_stop; break;
        }
        const uint32_t text = s_text;
        portEXIT_CRITICAL(&s_mux);
        if (!live || !fn) continue;
        if (n.kind == NOTE_HEAP) {
            if (!text) continue;
            memcpy((void *)(uintptr_t)text, n.text, NOTE_MAX);   /* this task is the only writer */
            n.a[0] = text;
        }
        uint32_t r = 0;
        const esp_err_t e = box_upcall(fn, n.a, &r);
        (void)e;                            /* the app stopping: INVALID_STATE, nothing to tell */
    }
    vTaskDelete(NULL);
}

/* Made by feckless_box_net_hooks(), under the app's lock, before any note. */
static bool notes_make(void)
{
    if (s_notes) return true;
    QueueHandle_t q = xQueueCreate(NOTES, sizeof(note_t));
    if (!q) return false;
    s_notes = q;
    if (xTaskCreatePinnedToCore(note_task, "box_wifi", NOTE_STACK, NULL, 4, NULL, tskNO_AFFINITY) != pdPASS) {
        vQueueDelete(q);
        s_notes = NULL;
        return false;
    }
    return true;
}

static void note(note_kind_t kind, const uint32_t a[4], const char *text)
{
    if (!s_notes) return;
    note_t n = { .kind = kind };
    portENTER_CRITICAL(&s_mux);
    n.gen = s_gen;
    portEXIT_CRITICAL(&s_mux);
    if (a) memcpy(n.a, a, sizeof n.a);
    if (text) strlcpy(n.text, text, sizeof n.text);
    if (xQueueSend(s_notes, &n, 0) != pdTRUE) ESP_LOGW(TAG, "note %d dropped: the app is behind", (int)kind);
}

/* ---- the launcher's hooks, standing for the app's -------------------- */

static uint32_t answers(void) { return __atomic_load_n(&s_answers, __ATOMIC_ACQUIRE); }
static void set_answers(uint32_t v) { __atomic_store_n(&s_answers, v, __ATOMIC_RELEASE); }

static bool t_wifi_enabled(void) { return (answers() & ANS_WIFI) != 0; }
static bool t_ntp_enabled(void) { return (answers() & ANS_NTP) != 0; }
static bool t_portal_running(void) { return (answers() & ANS_PORTAL) != 0; }
static void t_heap_note(const char *why) { note(NOTE_HEAP, NULL, why ? why : ""); }
static void t_online(void) { note(NOTE_ONLINE, NULL, NULL); }

static bool t_ntp_reply(int64_t epoch, int64_t boot_us)
{
    /* two int64 arguments: a0..a3, low word first (RV32 ilp32) */
    const uint32_t a[4] = { (uint32_t)epoch, (uint32_t)((uint64_t)epoch >> 32), (uint32_t)boot_us,
                            (uint32_t)((uint64_t)boot_us >> 32) };
    note(NOTE_NTP, a, NULL);
    return true;
}

static esp_err_t t_portal_stop(void)
{
    if (t_portal_running()) note(NOTE_PORTAL_STOP, NULL, NULL);
    return ESP_OK;
}

/* (heap_note, online, ntp_reply, portal_stop, answers): the app's
 * feckless_net_set_hooks(), from feckless_box_libc. Locked: the callback
 * loop is spawned here if it is not yet. An app with none of the notes
 * needs no loop. */
H(c_feckless_box_net_hooks)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    for (int i = 0; i < 4; i++)
        if (a[i] && !ok_ptr(w, a[i], 4)) return true;
    const bool notes = a[0] || a[1] || a[2] || a[3];
    if (notes) {
        if (box_upcall_start(w, LOOP_STACK, 5) != ESP_OK || !notes_make()) {
            *ret = (uint32_t)ESP_ERR_NO_MEM;
            return true;
        }
        if (!s_text) s_text = feckless_box_alloc(box_of_win(w), NOTE_MAX);
    }
    if (!s_saved) {
        s_launcher = *feckless_net_hooks();
        s_saved = true;
    }
    portENTER_CRITICAL(&s_mux);
    s_heap_note = a[0];
    s_online = a[1];
    s_ntp_reply = a[2];
    s_portal_stop = a[3];
    portEXIT_CRITICAL(&s_mux);
    set_answers(a[4]);
    const feckless_net_hooks_t h = {
        .heap_note = a[0] ? t_heap_note : NULL,
        .online = a[1] ? t_online : NULL,
        .ntp_enabled = t_ntp_enabled,
        .ntp_reply = a[2] ? t_ntp_reply : NULL,
        .wifi_enabled = t_wifi_enabled,
        .portal_running = t_portal_running,
        .portal_stop = t_portal_stop,
        .usb_register_class = NULL,         /* the launcher's, kept */
    };
    feckless_net_set_hooks(&h);             /* with a network up: SNTP and online(), queued */
    *ret = ESP_OK;
    return true;
}

/* ---- the switch ------------------------------------------------------- */

static void apply_job(void *arg) { *(esp_err_t *)arg = wifi_apply_settings(); }

/* (answers, how, why): how 0 wifi_apply_settings, 1 wifi_request_apply,
 * 2 wifi_request_restart(why). The answers first, so the request reads
 * them. */
H(c_feckless_box_wifi_apply)
{
    static char why[WHY_MAX];               /* wifi.c keeps the pointer, for its log */
    set_answers(a[0]);
    *ret = ESP_OK;
    switch (a[1]) {
    case 0: {
        esp_err_t e = ESP_FAIL;
        if (!box_flash_run(apply_job, &e)) e = ESP_ERR_NO_MEM;
        *ret = (uint32_t)e;
        break;
    }
    case 1:
        wifi_request_apply();
        break;
    case 2: {
        const char *s = a[2] ? str(w, a[2], WHY_MAX - 1) : NULL;
        portENTER_CRITICAL(&s_mux);
        strlcpy(why, s ? s : "", sizeof why);
        portEXIT_CRITICAL(&s_mux);
        wifi_request_restart(why);
        break;
    }
    default:
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    }
    return true;
}

/* ---- the station ------------------------------------------------------ */

/* (out, max) -> count or -1 */
H(c_wifi_scan_list)
{
    *ret = (uint32_t)-1;
    const int32_t max = (int32_t)a[1];
    if (max <= 0) return true;
    const int n = max > SCAN_MAX ? SCAN_MAX : max;
    if (!ok_ptr(w, a[0], (uint32_t)n * sizeof(wifi_seen_t))) return true;
    wifi_seen_t *seen = calloc((size_t)n, sizeof *seen);
    if (!seen) return true;
    const int got = wifi_scan_list(seen, n);
    if (got > 0) memcpy((void *)(uintptr_t)a[0], seen, (size_t)got * sizeof *seen);
    free(seen);
    *ret = (uint32_t)got;
    return true;
}

/* (ssid, secret, timeout_ms) */
H(c_wifi_join)
{
    const char *s = str(w, a[0], SSID_MAX);
    const char *p = a[1] ? str(w, a[1], SECRET_MAX) : NULL;
    if (!s || (a[1] && !p)) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    char ssid[SSID_MAX + 1], secret[SECRET_MAX + 1];
    strlcpy(ssid, s, sizeof ssid);
    strlcpy(secret, p ? p : "", sizeof secret);
    *ret = (uint32_t)wifi_join(ssid, p ? secret : NULL, a[2] > JOIN_MAX_MS ? JOIN_MAX_MS : a[2]);
    memset(secret, 0, sizeof secret);
    return true;
}

/* (ssid, passphrase, key[32]): on the flash task -- PBKDF2 through PSA,
 * whose buffers want internal RAM */
H(c_wifi_psk)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *s = str(w, a[0], SSID_MAX);
    const char *p = str(w, a[1], SECRET_MAX);
    if (!s || !p || !ok_ptr(w, a[2], 32)) return true;
    char ssid[SSID_MAX + 1], pass[SECRET_MAX + 1];
    uint8_t key[32];
    strlcpy(ssid, s, sizeof ssid);
    strlcpy(pass, p, sizeof pass);
    *ret = (uint32_t)wifi_psk(ssid, pass, key);
    memcpy((void *)(uintptr_t)a[2], key, sizeof key);
    memset(pass, 0, sizeof pass);
    memset(key, 0, sizeof key);
    return true;
}

/* (ssid, secret, is_psk): NVS, on the flash task */
H(c_wifistore_save)
{
    *ret = (uint32_t)ESP_ERR_INVALID_ARG;
    const char *s = str(w, a[0], SSID_MAX);
    const char *p = str(w, a[1], SECRET_MAX);
    if (!s || !p) return true;
    char ssid[SSID_MAX + 1], secret[SECRET_MAX + 1];
    strlcpy(ssid, s, sizeof ssid);
    strlcpy(secret, p, sizeof secret);
    *ret = (uint32_t)wifistore_save(ssid, secret, (a[2] & 0xff) != 0);
    memset(secret, 0, sizeof secret);
    return true;
}

/* ---- the access point (u-feckless-0084) ------------------------------- */

#define AP_PASS_MAX 63

/* (ssid, pass or 0) */
H(c_wifi_ap_begin_secure)
{
    const char *s = str(w, a[0], SSID_MAX);
    const char *p = a[1] ? str(w, a[1], AP_PASS_MAX) : NULL;
    if (!s || (a[1] && !p)) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    char ssid[SSID_MAX + 1], pass[AP_PASS_MAX + 1];
    strlcpy(ssid, s, sizeof ssid);
    strlcpy(pass, p ? p : "", sizeof pass);
    *ret = (uint32_t)wifi_ap_begin_secure(ssid, p ? pass : NULL);
    memset(pass, 0, sizeof pass);
    return true;
}

/* (ssid) */
H(c_wifi_ap_begin)
{
    const char *s = str(w, a[0], SSID_MAX);
    if (!s) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    char ssid[SSID_MAX + 1];
    strlcpy(ssid, s, sizeof ssid);
    *ret = (uint32_t)wifi_ap_begin(ssid);
    return true;
}

H(c_wifi_ap_end)
{
    (void)w; (void)a;
    *ret = (uint32_t)wifi_ap_end();
    return true;
}

H(c_wifi_ap_clients)
{
    (void)w; (void)a;
    *ret = (uint32_t)wifi_ap_clients();
    return true;
}

/* (out, size) -> bool */
H(c_wifi_ap_ip)
{
    *ret = 0;
    if (!a[1] || !ok_ptr(w, a[0], a[1])) return true;
    char ip[16];
    if (!wifi_ap_ip(ip, sizeof ip)) {
        *(char *)(uintptr_t)a[0] = '\0';
        return true;
    }
    strlcpy((char *)(uintptr_t)a[0], ip, a[1]);
    *ret = 1;
    return true;
}

/* (ifx, mac[6]) */
H(c_esp_wifi_get_mac)
{
    if (!ok_ptr(w, a[1], 6)) {
        *ret = (uint32_t)ESP_ERR_INVALID_ARG;
        return true;
    }
    uint8_t mac[6] = { 0 };
    *ret = (uint32_t)esp_wifi_get_mac((wifi_interface_t)a[0], mac);
    memcpy((void *)(uintptr_t)a[1], mac, sizeof mac);
    return true;
}

FLASH_WRAP(wifi_psk)
FLASH_WRAP(wifistore_save)

const feckless_umode_call_t box_wifi_calls[] = {
    FN(feckless_box_net_hooks), FNU(feckless_box_wifi_apply),
    FNU(wifi_scan_list), FNU(wifi_join), FLASHU(wifi_psk), FLASHU(wifistore_save),
    FNU(esp_wifi_get_mac),
    FNU(wifi_ap_begin_secure), FNU(wifi_ap_begin), FNU(wifi_ap_end), FNU(wifi_ap_clients), FNU(wifi_ap_ip),
};
const uint32_t box_wifi_calls_n = sizeof box_wifi_calls / sizeof box_wifi_calls[0];

/* The app has ended: its hooks out, the launcher's back, its notes dropped. */
void box_wifi_end(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool had = s_saved;
    s_gen++;
    s_heap_note = s_online = s_ntp_reply = s_portal_stop = 0;
    s_text = 0;                             /* went with the box */
    s_saved = false;
    portEXIT_CRITICAL(&s_mux);
    set_answers(ANS_WIFI);
    if (had) feckless_net_set_hooks(&s_launcher);
}

#else

const feckless_umode_call_t box_wifi_calls[1];
const uint32_t box_wifi_calls_n = 0;
void box_wifi_end(void) {}

#endif
