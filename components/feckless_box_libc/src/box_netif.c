/* SPDX-License-Identifier: MIT
 *
 * esp_netif_find_if() for a boxed app (u-feckless-0076), run in the app.
 *
 * Its predicate is the app's code, so it is called here, in the app, for
 * each interface the host numbers (feckless_box_calls' box_netif.c), and
 * never by the host. The predicate may ask the host about the interface
 * it is given -- esp_netif_get_ip_info() and the rest take the number --
 * and the host finds it again for each such call, so an interface pulled
 * out mid-search answers ESP_ERR_INVALID_ARG rather than a freed pointer.
 * The list is the host's as of each step, not one held for the whole
 * search as IDF's is: an interface added or removed meanwhile is seen or
 * missed, as it would be a moment earlier or later.
 *
 * Linked only into an ELF that calls esp_netif_find_if(): the linker
 * takes this file for that name.
 */
#include <stdbool.h>
#include <stdint.h>

typedef bool (*box_netif_pred_t)(void *netif, void *ctx);

uint32_t feckless_box_netif_next(uint32_t prev);   /* the host's (feckless_abi's box section) */

void *esp_netif_find_if(box_netif_pred_t fn, void *ctx)
{
    if (!fn) return 0;
    for (uint32_t n = feckless_box_netif_next(0); n; n = feckless_box_netif_next(n)) {
        if (fn((void *)(uintptr_t)n, ctx)) return (void *)(uintptr_t)n;
    }
    return 0;
}
