/* SPDX-License-Identifier: MIT
 *
 * u-feckless-0081: feckless_fetch as box_fetch.c sees it. A start keeps
 * its options (copied, as the real one copies them); a test plays the
 * download's task with fake_fetch_event(). A cancel of a download that is
 * not running delivers CANCELLED on the caller's task, as the real one
 * does.
 */
#include <stdio.h>
#include <string.h>

#include "feckless_fetch.h"

feckless_fetch_opts_t fake_fetch_opts;
char fake_fetch_url[FECKLESS_FETCH_URL_MAX], fake_fetch_path[FECKLESS_FETCH_PATH_MAX],
    fake_fetch_partp[FECKLESS_FETCH_PATH_MAX], fake_fetch_ua[128];
bool fake_fetch_running, fake_fetch_paused;
int fake_fetch_starts, fake_fetch_pauses, fake_fetch_discards;
char fake_fetch_discarded[FECKLESS_FETCH_PATH_MAX];

bool feckless_fetch_start(const feckless_fetch_opts_t *o)
{
    if (fake_fetch_running || !o->url || !o->path) return false;
    fake_fetch_opts = *o;
    snprintf(fake_fetch_url, sizeof fake_fetch_url, "%s", o->url);
    snprintf(fake_fetch_path, sizeof fake_fetch_path, "%s", o->path);
    snprintf(fake_fetch_partp, sizeof fake_fetch_partp, "%s", o->part ? o->part : "");
    snprintf(fake_fetch_ua, sizeof fake_fetch_ua, "%s", o->user_agent ? o->user_agent : "");
    fake_fetch_running = true;
    fake_fetch_starts++;
    return true;
}

bool fake_fetch_event(feckless_fetch_ev_kind_t kind, const char *url, const char *ctype, int64_t size)
{
    if (!fake_fetch_opts.cb) return true;
    feckless_fetch_ev_t ev = { .kind = kind, .id = fake_fetch_opts.id, .url = url, .ctype = ctype,
                               .part = fake_fetch_partp, .path = fake_fetch_path,
                               .path_size = sizeof fake_fetch_path, .size = size, .arg = fake_fetch_opts.arg };
    return fake_fetch_opts.cb(&ev);
}

void feckless_fetch_pause(void) { fake_fetch_pauses++; fake_fetch_running = false; fake_fetch_paused = true; }
void feckless_fetch_cancel(void)
{
    if (fake_fetch_running) { fake_fetch_running = false; return; }   /* CANCELLED on the download's task */
    fake_fetch_paused = false;
    (void)fake_fetch_event(FECKLESS_FETCH_EV_CANCELLED, NULL, NULL, 0);   /* on the caller's */
}
feckless_fetch_state_t feckless_fetch_state(int64_t *id, int64_t *got, int64_t *total, char *path, size_t n,
                                            uint32_t *gen)
{
    if (id) *id = fake_fetch_opts.id;
    if (got) *got = 0x123456789LL;
    if (total) *total = 0x223456789LL;
    if (path && n) snprintf(path, n, "%s", fake_fetch_path);
    if (gen) *gen = 7;
    return fake_fetch_running ? FECKLESS_FETCH_RUNNING : fake_fetch_paused ? FECKLESS_FETCH_PAUSED : FECKLESS_FETCH_IDLE;
}
void feckless_fetch_why(char *out, size_t n) { snprintf(out, n, "one is running"); }
bool feckless_fetch_live(const char *path, char *part, size_t n, int64_t *ready, int64_t *total,
                         feckless_fetch_state_t *st)
{
    if (strcmp(path, fake_fetch_path)) return false;
    if (part && n) snprintf(part, n, "%s", fake_fetch_partp);
    if (ready) *ready = 1 << 20;
    if (total) *total = 3 << 20;
    if (st) *st = fake_fetch_running ? FECKLESS_FETCH_RUNNING : FECKLESS_FETCH_PAUSED;
    return true;
}
bool feckless_fetch_part(const char *part, int64_t *size)
{
    if (strcmp(part, fake_fetch_partp)) return false;
    if (size) *size = 1 << 20;
    return true;
}
void feckless_fetch_discard(const char *part)
{
    fake_fetch_discards++;
    snprintf(fake_fetch_discarded, sizeof fake_fetch_discarded, "%s", part);
}
