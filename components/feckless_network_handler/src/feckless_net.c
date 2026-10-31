/*
 * feckless_net.c -- the application's hooks. See feckless_net.h.
 *
 * SPDX-License-Identifier: MIT
 */

#include "feckless_net.h"

static feckless_net_hooks_t s_hooks;

void feckless_net_set_hooks(const feckless_net_hooks_t *hooks)
{
    if (hooks) s_hooks = *hooks;
    else       s_hooks = (feckless_net_hooks_t){ 0 };
}

const feckless_net_hooks_t *feckless_net_hooks(void)
{
    return &s_hooks;
}
