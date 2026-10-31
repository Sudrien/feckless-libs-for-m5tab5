/*
 * feckless_net.h -- what this component asks of the application.
 *
 * wifi.c and ethernet.c were written inside a music player, and in it
 * they reached out to five of its modules by name: settings (is the
 * radio wanted, is NTP wanted, is this NTP reply believable), the setup
 * portal (is it running on the radio, stop it), the stream probe (kick
 * it once a network is up), the heap map (log a baseline) and the USB
 * host (register a class driver on the one bus). None of those are
 * networking, and none of them can come with this component.
 *
 * So they are hooks, set once before wifi_init() and ethernet_init(),
 * and every call site that used to name a player function calls the
 * hook in the same place instead. The order of events at each site is
 * unchanged; that is the point of keeping them as separate hooks rather
 * than one "something happened" callback.
 *
 * Every hook may be NULL. The defaults are what a program with no
 * opinion would want, and are listed beside each one.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A class driver's install function, as the USB host owner takes it. */
typedef esp_err_t (*feckless_net_usb_class_fn)(void);

typedef struct {
    /* A memory baseline worth logging: "station up", "ethernet up".
     * Called before the interface is marked connected.
     * NULL: nothing. */
    void (*heap_note)(const char *why);

    /* A network has just become usable (station address, or cable link
     * plus address). Called after the interface is marked connected, and
     * on Wi-Fi after NTP has been started.
     * NULL: nothing. */
    void (*online)(void);

    /* Whether to start SNTP once the station has an address.
     * NULL: no SNTP. A library should not phone time servers unasked. */
    bool (*ntp_enabled)(void);

    /* An NTP reply arrived and the system clock has already moved to it.
     * Return whether the reply was plausible; wifi_ntp_synced() follows
     * the answer. `boot_us` is esp_timer_get_time() at the reply.
     * NULL: every reply is believed. */
    bool (*ntp_reply)(int64_t epoch, int64_t boot_us);

    /* Whether the radio should be on, read by wifi_apply_settings().
     * NULL: always on. */
    bool (*wifi_enabled)(void);

    /* Whether something else (a setup portal) has the radio in AP mode,
     * in which case the background join stays out of its way.
     * NULL: never. */
    bool (*portal_running)(void);

    /* Bring that something down before the radio is switched off. May
     * block until it has stopped. NULL: nothing to stop. */
    esp_err_t (*portal_stop)(void);

    /* Register a USB class driver with whatever owns the USB host. The
     * Ethernet driver is one class among several on the Tab5's port, so
     * this component does not own the bus.
     * NULL: ethernet_init() returns ESP_ERR_INVALID_STATE. */
    esp_err_t (*usb_register_class)(const char *name,
                                    feckless_net_usb_class_fn fn);
} feckless_net_hooks_t;

/*
 * Copy the hooks in. Once, before wifi_init() and ethernet_init(); the
 * callers read them from other tasks without a lock, so changing them
 * afterwards is not supported.
 */
void feckless_net_set_hooks(const feckless_net_hooks_t *hooks);

/* The hooks in force; never NULL, members may be. For wifi.c and
 * ethernet.c. */
const feckless_net_hooks_t *feckless_net_hooks(void);

#ifdef __cplusplus
}
#endif
