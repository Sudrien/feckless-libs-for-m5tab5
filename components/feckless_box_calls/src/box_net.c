/* SPDX-License-Identifier: MIT
 *
 * The network for a boxed app (u-feckless-0071): what the launcher's
 * feckless_network_handler says about the routes, and lwIP's sockets, so
 * an app can stream, serve and look names up over whatever network the
 * launcher brought up -- a cable at once, the radio when it is on.
 *
 * Not here yet: the radio's control and policy (wifi_request_apply() asks
 * the launcher's hooks, and the app's are functions a launcher does not
 * call), esp_netif's handles, the HTTP server and client, feckless_fetch
 * and feckless_https -- every one of those calls back into the app.
 *
 * SOCKETS
 * - The app is given lwIP's own descriptor, and may use only those it was
 *   given: a table of SOCKS per app, and any other number is EBADF before
 *   lwIP sees it -- including one select() was asked to watch. When the
 *   app ends (box_net_end()) every socket it left open is closed.
 * - Every socket is non-blocking in lwIP, always; blocking is the app's
 *   view, kept here per socket (fcntl()'s O_NONBLOCK, SO_RCVTIMEO,
 *   SO_SNDTIMEO) and done by waiting in select() slices of SLICE_MS. So a
 *   recv() with nothing to read waits as the app asked, but gives up
 *   (EINTR) when the app is stopping, like every wait in a box, and no
 *   call is ever stuck inside lwIP where the stop cannot reach it. All of
 *   these calls are unlocked: one thread waiting in accept() does not hold
 *   up the others.
 * - A blocking connect() is lwIP's non-blocking one and a wait to be
 *   writable, then SO_ERROR; a blocking send() sends it all, as lwIP's
 *   does, or what it could before a timeout or an error.
 * - Addresses, options and the fd_sets are copied in before the call and
 *   out after it, so another of the app's threads cannot change them under
 *   lwIP; the data of send() and recv() are read and written in place,
 *   checked to lie in the box, at most IO_MAX a call.
 * - getaddrinfo() waits unlocked for the lookup, then takes the app's lock
 *   only to allocate the answer in the box: one block holding the list,
 *   its addresses and the canonical name, which freeaddrinfo() frees. At
 *   most AI_MAX entries, and AI_HELD answers outstanding at once.
 *
 * THE ROUTES
 * - net_online, ethernet_connected, ethernet_link, ethernet_ip, wifi_up,
 *   wifi_connected, wifi_ntp_synced, wifi_sta_ip, wifi_sta_ssid,
 *   wifistore_count, net_route_describe: read only, strings copied out
 *   through the host's stack. net_probe's timeout is capped at
 *   PROBE_MAX_MS, so its three waits end inside a box's stop.
 *
 * Without feckless_network_handler in the launcher, or with
 * FECKLESS_BOX_NO_NET, the table is empty, and an app that takes these
 * names is refused at its load by name.
 */
#include <string.h>

#include "box_calls_priv.h"

#if __has_include("ethernet.h") && __has_include("lwip/sockets.h") && !defined(FECKLESS_BOX_NO_NET)

#include <errno.h>
#include <fcntl.h>

#include "freertos/FreeRTOS.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include "ethernet.h"
#include "wifi.h"
#include "wifistore.h"

#include "feckless_umode_box.h"
#include "feckless_umode_load.h"

#define SOCKS         16                    /* an app's sockets at once */
#define SLICE_MS      100                   /* box_rtos.c's */
#define IO_MAX        (64 * 1024)           /* bytes a send or recv moves */
#define OPT_MAX       64                    /* an option's value, bytes */
#define STR_MAX       128                   /* a name or address string */
#define NODE_MAX      255                   /* a host name to look up */
#define SERV_MAX      32
#define AI_MAX        8                     /* entries in one answer */
#define AI_HELD       8                     /* answers not yet freed */
#define PROBE_MAX_MS  600                   /* three of them under the stop's wait */

typedef struct {
    int fd;
    bool used;
    bool nonblock;                          /* the app's O_NONBLOCK */
    uint32_t rcv_ms, snd_ms;                /* the app's SO_RCVTIMEO, SO_SNDTIMEO; 0 none */
} sock_t;

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static sock_t s_sock[SOCKS];
static uintptr_t s_ai[AI_HELD];             /* getaddrinfo() answers in the box */

static const uint32_t s_network_v1 = 1;     /* feckless_abi_network_v1 */

static bool stopping(void) { return feckless_umode_app_stopping(feckless_umode_app_loaded()); }

/* ---- the app's sockets ------------------------------------------------- */

/* A copy of the app's socket fd, false if it is not one. */
static bool sock_get(int fd, sock_t *out)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SOCKS; i++) {
        if (s_sock[i].used && s_sock[i].fd == fd) {
            if (out) *out = s_sock[i];
            ok = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static bool sock_add(int fd)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SOCKS; i++) {
        if (!s_sock[i].used) {
            s_sock[i] = (sock_t){ .fd = fd, .used = true };
            ok = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static bool sock_drop(int fd)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SOCKS; i++) {
        if (s_sock[i].used && s_sock[i].fd == fd) {
            s_sock[i].used = false;
            ok = true;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static void sock_set(int fd, int what, uint32_t v)
{
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < SOCKS; i++) {
        if (s_sock[i].used && s_sock[i].fd == fd) {
            if (what == 0) s_sock[i].nonblock = v != 0;
            else if (what == 1) s_sock[i].rcv_ms = v;
            else s_sock[i].snd_ms = v;
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
}

/* lwIP's side always non-blocking; the app's starts blocking. */
static bool adopt(win_t *w, int fd)
{
    const int fl = lwip_fcntl(fd, F_GETFL, 0);
    if (fl < 0 || lwip_fcntl(fd, F_SETFL, fl | O_NONBLOCK) < 0 || !sock_add(fd)) {
        lwip_close(fd);
        box_set_errno(w, EMFILE);
        return false;
    }
    return true;
}

/* Wait for fd to be readable (or writable), ms at most (0: no limit), in
 * slices, giving up when the app stops. 1 ready, 0 timed out, -1 failed
 * with errno set (EINTR for the stop). The time left is counted down by
 * the slices that ran out, not read from a clock: select() returns before
 * its slice only when it has something to say. */
static int wait_fd(int fd, bool wr, uint32_t ms)
{
    uint32_t left = ms;
    for (;;) {
        if (stopping()) { errno = EINTR; return -1; }
        const uint32_t slice = ms && left < SLICE_MS ? left : SLICE_MS;
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        struct timeval tv = { .tv_sec = 0, .tv_usec = (long)slice * 1000 };
        const int r = lwip_select(fd + 1, wr ? NULL : &set, wr ? &set : NULL, NULL, &tv);
        if (r > 0) return 1;
        if (r < 0 && errno != EINTR) return -1;
        if (r == 0 && ms) {
            left -= slice;
            if (!left) return 0;
        }
    }
}

static bool again(int e) { return e == EAGAIN || e == EWOULDBLOCK; }

/* An address the app passed: len checked, then copied. */
static bool addr_in(win_t *w, uint32_t p, uint32_t len, struct sockaddr_storage *ss)
{
    if (!p || len > sizeof *ss || !ok_ptr(w, p, len)) return false;
    memset(ss, 0, sizeof *ss);
    memcpy(ss, (const void *)(uintptr_t)p, len);
    return true;
}

/* An (addr, socklen_t *len) the app passed to be filled: NULL addr is
 * allowed (the address is dropped). The length is read once, here, and
 * that checked room is all addr_out() writes: another of the app's threads
 * changing *len meanwhile cannot widen it. */
static bool addr_out_ok(win_t *w, uint32_t p, uint32_t lenp, socklen_t *room)
{
    *room = 0;
    if (!p) return true;
    if (!ok_ptr(w, lenp, sizeof(socklen_t))) return false;
    const socklen_t l = *(const socklen_t *)(uintptr_t)lenp;
    if (!ok_ptr(w, p, l)) return false;
    *room = l;
    return true;
}

static void addr_out(uint32_t p, uint32_t lenp, socklen_t room, const struct sockaddr_storage *ss, socklen_t got)
{
    if (!p) return;
    memcpy((void *)(uintptr_t)p, ss, got < room ? got : room);
    *(socklen_t *)(uintptr_t)lenp = got;
}

static int fail(win_t *w, int e)
{
    box_set_errno(w, e);
    return -1;
}

/* (domain, type, protocol) */
H(c_lwip_socket)
{
    int fd = lwip_socket((int)a[0], (int)a[1], (int)a[2]);
    if (fd < 0) fd = fail(w, errno);
    else if (!adopt(w, fd)) fd = -1;
    *ret = (uint32_t)fd;
    return true;
}

/* (s, name, namelen) */
H(c_lwip_bind)
{
    struct sockaddr_storage ss;
    int r;
    if (!sock_get((int)a[0], NULL)) r = fail(w, EBADF);
    else if (!addr_in(w, a[1], a[2], &ss)) r = fail(w, EFAULT);
    else if ((r = lwip_bind((int)a[0], (struct sockaddr *)&ss, (socklen_t)a[2])) < 0) r = fail(w, errno);
    *ret = (uint32_t)r;
    return true;
}

H(c_lwip_connect)
{
    struct sockaddr_storage ss;
    sock_t s;
    int r;
    if (!sock_get((int)a[0], &s)) r = fail(w, EBADF);
    else if (!addr_in(w, a[1], a[2], &ss)) r = fail(w, EFAULT);
    else if ((r = lwip_connect(s.fd, (struct sockaddr *)&ss, (socklen_t)a[2])) < 0) {
        const int e = errno;
        if (e != EINPROGRESS || s.nonblock) {
            r = fail(w, e);
        } else {
            const int k = wait_fd(s.fd, true, s.snd_ms);
            if (k < 0) {
                r = fail(w, errno);
            } else if (k == 0) {
                r = fail(w, ETIMEDOUT);
            } else {
                int err = 0;
                socklen_t el = sizeof err;
                if (lwip_getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &err, &el) < 0) err = errno;
                r = err ? fail(w, err) : 0;
            }
        }
    }
    *ret = (uint32_t)r;
    return true;
}

H(c_lwip_listen)
{
    int r;
    if (!sock_get((int)a[0], NULL)) r = fail(w, EBADF);
    else if ((r = lwip_listen((int)a[0], (int)a[1])) < 0) r = fail(w, errno);
    *ret = (uint32_t)r;
    return true;
}

/* (s, addr, addrlen) */
H(c_lwip_accept)
{
    sock_t s;
    socklen_t room;
    int r = -1;
    if (!sock_get((int)a[0], &s)) { r = fail(w, EBADF); goto out; }
    if (!addr_out_ok(w, a[1], a[2], &room)) { r = fail(w, EFAULT); goto out; }
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t l = sizeof ss;
        r = lwip_accept(s.fd, (struct sockaddr *)&ss, &l);
        if (r >= 0) {
            if (adopt(w, r)) addr_out(a[1], a[2], room, &ss, l);
            else r = -1;
            break;
        }
        const int e = errno;
        if (!again(e) || s.nonblock) { r = fail(w, e); break; }
        const int k = wait_fd(s.fd, false, s.rcv_ms);
        if (k <= 0) { r = fail(w, k < 0 ? errno : EAGAIN); break; }
    }
out:
    *ret = (uint32_t)r;
    return true;
}

/* The send loop for send() and sendto(): all of it if blocking, as lwIP's
 * own would; what went before a timeout or an error, else -1. */
static int send_some(win_t *w, const sock_t *s, const uint8_t *buf, uint32_t len, int flags,
                     const struct sockaddr *to, socklen_t tolen)
{
    const bool block = !s->nonblock && !(flags & MSG_DONTWAIT);
    uint32_t done = 0;
    for (;;) {
        const int r = to ? lwip_sendto(s->fd, buf + done, len - done, flags | MSG_DONTWAIT, to, tolen)
                         : lwip_send(s->fd, buf + done, len - done, flags | MSG_DONTWAIT);
        if (r > 0) {
            done += (uint32_t)r;
            if (done >= len || to) return (int)done;    /* a datagram goes whole or not at all */
            continue;
        }
        if (r == 0 && len == 0) return 0;
        const int e = r < 0 ? errno : EAGAIN;
        if (!again(e) || !block) return done ? (int)done : fail(w, e);
        const int k = wait_fd(s->fd, true, s->snd_ms);
        if (k <= 0) return done ? (int)done : fail(w, k < 0 ? errno : EAGAIN);
    }
}

/* (s, data, size, flags) */
H(c_lwip_send)
{
    sock_t s;
    int r;
    if (!sock_get((int)a[0], &s)) r = fail(w, EBADF);
    else if (a[2] > IO_MAX || !ok_ptr(w, a[1], a[2])) r = fail(w, a[2] > IO_MAX ? EMSGSIZE : EFAULT);
    else r = send_some(w, &s, (const uint8_t *)(uintptr_t)a[1], a[2], (int)a[3], NULL, 0);
    *ret = (uint32_t)r;
    return true;
}

/* (s, data, size, flags, to, tolen) */
H(c_lwip_sendto)
{
    sock_t s;
    struct sockaddr_storage ss;
    int r;
    if (!sock_get((int)a[0], &s)) r = fail(w, EBADF);
    else if (a[2] > IO_MAX || !ok_ptr(w, a[1], a[2])) r = fail(w, a[2] > IO_MAX ? EMSGSIZE : EFAULT);
    else if (a[4] && !addr_in(w, a[4], a[5], &ss)) r = fail(w, EFAULT);
    else r = send_some(w, &s, (const uint8_t *)(uintptr_t)a[1], a[2], (int)a[3],
                       a[4] ? (struct sockaddr *)&ss : NULL, (socklen_t)a[5]);
    *ret = (uint32_t)r;
    return true;
}

/* recv() and recvfrom(): from is NULL for recv. */
static int recv_some(win_t *w, const sock_t *s, uint8_t *buf, uint32_t len, int flags,
                     uint32_t from, uint32_t fromlen, socklen_t room)
{
    const bool block = !s->nonblock && !(flags & MSG_DONTWAIT);
    for (;;) {
        struct sockaddr_storage ss;
        socklen_t l = sizeof ss;
        const int r = from ? lwip_recvfrom(s->fd, buf, len, flags | MSG_DONTWAIT, (struct sockaddr *)&ss, &l)
                           : lwip_recv(s->fd, buf, len, flags | MSG_DONTWAIT);
        if (r >= 0) {
            if (from) addr_out(from, fromlen, room, &ss, l);
            return r;
        }
        const int e = errno;
        if (!again(e) || !block) return fail(w, e);
        const int k = wait_fd(s->fd, false, s->rcv_ms);
        if (k <= 0) return fail(w, k < 0 ? errno : EAGAIN);
    }
}

/* (s, mem, len, flags) */
H(c_lwip_recv)
{
    sock_t s;
    int r;
    if (!sock_get((int)a[0], &s)) r = fail(w, EBADF);
    else if (a[2] > IO_MAX || !ok_ptr(w, a[1], a[2])) r = fail(w, a[2] > IO_MAX ? EMSGSIZE : EFAULT);
    else r = recv_some(w, &s, (uint8_t *)(uintptr_t)a[1], a[2], (int)a[3], 0, 0, 0);
    *ret = (uint32_t)r;
    return true;
}

/* (s, mem, len, flags, from, fromlen) */
H(c_lwip_recvfrom)
{
    sock_t s;
    socklen_t room;
    int r;
    if (!sock_get((int)a[0], &s)) r = fail(w, EBADF);
    else if (a[2] > IO_MAX || !ok_ptr(w, a[1], a[2])) r = fail(w, a[2] > IO_MAX ? EMSGSIZE : EFAULT);
    else if (!addr_out_ok(w, a[4], a[5], &room)) r = fail(w, EFAULT);
    else r = recv_some(w, &s, (uint8_t *)(uintptr_t)a[1], a[2], (int)a[3], a[4], a[5], room);
    *ret = (uint32_t)r;
    return true;
}

static uint32_t tv_ms(const struct timeval *tv)
{
    if (tv->tv_sec < 0 || tv->tv_usec < 0) return 0;
    const uint64_t ms = (uint64_t)tv->tv_sec * 1000u + (uint64_t)tv->tv_usec / 1000u;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)(ms ? ms : (tv->tv_usec ? 1 : 0));
}

/* (s, level, optname, optval, optlen) */
H(c_lwip_setsockopt)
{
    uint8_t v[OPT_MAX];
    int r;
    if (!sock_get((int)a[0], NULL)) r = fail(w, EBADF);
    else if (a[4] > OPT_MAX || (a[4] && !ok_ptr(w, a[3], a[4]))) r = fail(w, a[4] > OPT_MAX ? EINVAL : EFAULT);
    else {
        if (a[4]) memcpy(v, (const void *)(uintptr_t)a[3], a[4]);
        r = lwip_setsockopt((int)a[0], (int)a[1], (int)a[2], a[4] ? v : NULL, (socklen_t)a[4]);
        if (r < 0) {
            r = fail(w, errno);
        } else if ((int)a[1] == SOL_SOCKET && ((int)a[2] == SO_RCVTIMEO || (int)a[2] == SO_SNDTIMEO) &&
                   a[4] >= sizeof(struct timeval)) {
            struct timeval tv;
            memcpy(&tv, v, sizeof tv);
            sock_set((int)a[0], (int)a[2] == SO_RCVTIMEO ? 1 : 2, tv_ms(&tv));
        }
    }
    *ret = (uint32_t)r;
    return true;
}

/* (s, level, optname, optval, optlen *) */
H(c_lwip_getsockopt)
{
    uint8_t v[OPT_MAX];
    int r;
    if (!sock_get((int)a[0], NULL)) { r = fail(w, EBADF); goto out; }
    if (!ok_ptr(w, a[4], sizeof(socklen_t))) { r = fail(w, EFAULT); goto out; }
    socklen_t *lp = (socklen_t *)(uintptr_t)a[4];
    const socklen_t room = *lp > OPT_MAX ? OPT_MAX : *lp;     /* read once: what was checked */
    if (room && !ok_ptr(w, a[3], room)) { r = fail(w, EFAULT); goto out; }
    socklen_t l = room;
    memset(v, 0, sizeof v);
    r = lwip_getsockopt((int)a[0], (int)a[1], (int)a[2], v, &l);
    if (r < 0) {
        r = fail(w, errno);
    } else {
        memcpy((void *)(uintptr_t)a[3], v, l < room ? l : room);
        *lp = l;
    }
out:
    *ret = (uint32_t)r;
    return true;
}

typedef int (*name_fn)(int, struct sockaddr *, socklen_t *);

static int name_of(win_t *w, const uint32_t a[7], name_fn fn)
{
    socklen_t room;
    if (!sock_get((int)a[0], NULL)) return fail(w, EBADF);
    if (!a[1] || !addr_out_ok(w, a[1], a[2], &room)) return fail(w, EFAULT);
    struct sockaddr_storage ss;
    socklen_t l = sizeof ss;
    if (fn((int)a[0], (struct sockaddr *)&ss, &l) < 0) return fail(w, errno);
    addr_out(a[1], a[2], room, &ss, l);
    return 0;
}

H(c_lwip_getsockname) { *ret = (uint32_t)name_of(w, a, lwip_getsockname); return true; }
H(c_lwip_getpeername) { *ret = (uint32_t)name_of(w, a, lwip_getpeername); return true; }

H(c_lwip_shutdown)
{
    int r;
    if (!sock_get((int)a[0], NULL)) r = fail(w, EBADF);
    else if ((r = lwip_shutdown((int)a[0], (int)a[1])) < 0) r = fail(w, errno);
    *ret = (uint32_t)r;
    return true;
}

/* lwip_close(), and close() -- which in a box is a socket's: its files are
 * FILEs and FILs, and fileno()'s numbers are fsync()'s alone. */
H(c_lwip_close)
{
    int r;
    if (!sock_drop((int)a[0])) r = fail(w, EBADF);
    else if ((r = lwip_close((int)a[0])) < 0) r = fail(w, errno);
    *ret = (uint32_t)r;
    return true;
}

H(c_close) { return c_lwip_close(w, a, ret); }

/* (s, cmd, val): F_GETFL and F_SETFL, O_NONBLOCK the app's own */
H(c_lwip_fcntl)
{
    sock_t s;
    int r;
    if (!sock_get((int)a[0], &s)) {
        r = fail(w, EBADF);
    } else if ((int)a[1] == F_GETFL) {
        r = lwip_fcntl(s.fd, F_GETFL, 0);
        if (r < 0) r = fail(w, errno);
        else r = (r & ~O_NONBLOCK) | (s.nonblock ? O_NONBLOCK : 0);
    } else if ((int)a[1] == F_SETFL) {
        r = lwip_fcntl(s.fd, F_SETFL, (int)a[2] | O_NONBLOCK);
        if (r < 0) r = fail(w, errno);
        else sock_set(s.fd, 0, ((int)a[2] & O_NONBLOCK) != 0);
    } else {
        r = fail(w, ENOSYS);                /* as lwIP answers any other */
    }
    *ret = (uint32_t)r;
    return true;
}

/* (nfds, readfds, writefds, exceptfds, timeout): each set NULL or a whole
 * fd_set in the box, every fd in it the app's; the timeout NULL (no limit)
 * or a timeval. Waits in slices; the stop ends it with EINTR. */
H(c_select)
{
    const int nfds = (int)a[0];
    fd_set in[3], out[3];
    bool has[3];
    int r = 0;
    if (nfds < 0 || nfds > FD_SETSIZE) { r = fail(w, EINVAL); goto out; }
    for (int k = 0; k < 3; k++) {
        has[k] = a[1 + k] != 0;
        FD_ZERO(&in[k]);
        if (!has[k]) continue;
        if (!ok_ptr(w, a[1 + k], sizeof(fd_set))) { r = fail(w, EFAULT); goto out; }
        memcpy(&in[k], (const void *)(uintptr_t)a[1 + k], sizeof(fd_set));
        for (int fd = 0; fd < nfds; fd++) {
            if (FD_ISSET(fd, &in[k]) && !sock_get(fd, NULL)) { r = fail(w, EBADF); goto out; }
        }
    }
    bool forever = true;
    uint32_t ms = 0;
    if (a[4]) {
        if (!ok_ptr(w, a[4], sizeof(struct timeval))) { r = fail(w, EFAULT); goto out; }
        struct timeval tv;
        memcpy(&tv, (const void *)(uintptr_t)a[4], sizeof tv);
        forever = false;
        ms = tv_ms(&tv);
    }
    uint32_t left = ms;                     /* counted down by slices, as wait_fd() */
    for (;;) {
        if (stopping()) { r = fail(w, EINTR); goto out; }
        const uint32_t slice = !forever && left < SLICE_MS ? left : SLICE_MS;
        for (int k = 0; k < 3; k++) out[k] = in[k];
        struct timeval tv = { .tv_sec = 0, .tv_usec = (long)slice * 1000 };
        r = lwip_select(nfds, has[0] ? &out[0] : NULL, has[1] ? &out[1] : NULL, has[2] ? &out[2] : NULL, &tv);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) { r = fail(w, errno); goto out; }
        if (r > 0) break;
        if (!forever) {
            left -= slice;
            if (!left) break;               /* r is 0: the sets come back empty */
        }
    }
    for (int k = 0; k < 3; k++) {
        if (has[k]) memcpy((void *)(uintptr_t)a[1 + k], &out[k], sizeof(fd_set));
    }
out:
    *ret = (uint32_t)r;
    return true;
}

/* ---- names ------------------------------------------------------------- */

static bool ai_hold(uintptr_t p)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < AI_HELD; i++) {
        if (!s_ai[i]) { s_ai[i] = p; ok = true; break; }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static bool ai_release(uintptr_t p)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < AI_HELD; i++) {
        if (p && s_ai[i] == p) { s_ai[i] = 0; ok = true; break; }
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static void box_free_locked(win_t *w, uintptr_t p)
{
    feckless_umode_app_t *app = feckless_umode_app_loaded();
    feckless_umode_app_lock(app);
    feckless_box_free(box_of_win(w), p);
    feckless_umode_app_unlock(app);
}

/* (nodename, servname, hints, res): 0 or an EAI_ code, as lwIP's */
H(c_lwip_getaddrinfo)
{
    char node[NODE_MAX + 1], serv[SERV_MAX + 1];
    const char *n = NULL, *sv = NULL;
    struct addrinfo h, *got = NULL;
    int r;
    if (a[0]) {
        n = str(w, a[0], NODE_MAX + 1);
        if (!n) { r = EAI_FAIL; goto out; }
        strcpy(node, n);
        n = node;
    }
    if (a[1]) {
        sv = str(w, a[1], SERV_MAX + 1);
        if (!sv) { r = EAI_FAIL; goto out; }
        strcpy(serv, sv);
        sv = serv;
    }
    if (a[2]) {
        if (!ok_ptr(w, a[2], sizeof h)) { r = EAI_FAIL; goto out; }
        memcpy(&h, (const void *)(uintptr_t)a[2], sizeof h);
        h.ai_addr = NULL;                   /* the app's pointers: not lwIP's to follow */
        h.ai_canonname = NULL;
        h.ai_next = NULL;
    }
    if (!ok_ptr(w, a[3], sizeof(struct addrinfo *))) { r = EAI_FAIL; goto out; }

    r = lwip_getaddrinfo(n, sv, a[2] ? &h : NULL, &got);   /* the wait, unlocked */
    if (r != 0) goto out;

    /* One block in the box: the entries, their addresses, the names. */
    uint32_t count = 0, names = 0;
    for (const struct addrinfo *p = got; p && count < AI_MAX; p = p->ai_next, count++) {
        if (p->ai_canonname) names += (uint32_t)strnlen(p->ai_canonname, NODE_MAX) + 1;
    }
    const uint32_t size = count * (uint32_t)(sizeof(struct addrinfo) + sizeof(struct sockaddr_storage)) + names;
    feckless_umode_app_t *app = feckless_umode_app_loaded();
    feckless_umode_app_lock(app);
    const uintptr_t blk = count ? feckless_box_alloc(box_of_win(w), size) : 0;
    feckless_umode_app_unlock(app);
    if (!blk || !ai_hold(blk)) {
        if (blk) box_free_locked(w, blk);
        lwip_freeaddrinfo(got);
        r = EAI_MEMORY;
        goto out;
    }
    struct addrinfo *ai = (struct addrinfo *)blk;
    struct sockaddr_storage *sa = (struct sockaddr_storage *)(ai + count);
    char *nm = (char *)(sa + count);
    const struct addrinfo *p = got;
    for (uint32_t i = 0; i < count; i++, p = p->ai_next) {
        ai[i] = *p;
        const socklen_t l = p->ai_addrlen < sizeof sa[i] ? p->ai_addrlen : (socklen_t)sizeof sa[i];
        memset(&sa[i], 0, sizeof sa[i]);
        if (p->ai_addr) memcpy(&sa[i], p->ai_addr, l);
        ai[i].ai_addr = p->ai_addr ? (struct sockaddr *)&sa[i] : NULL;
        ai[i].ai_addrlen = l;
        ai[i].ai_canonname = NULL;
        if (p->ai_canonname) {
            const size_t cl = strnlen(p->ai_canonname, NODE_MAX);
            memcpy(nm, p->ai_canonname, cl);
            nm[cl] = '\0';
            ai[i].ai_canonname = nm;
            nm += cl + 1;
        }
        ai[i].ai_next = i + 1 < count ? &ai[i + 1] : NULL;
    }
    lwip_freeaddrinfo(got);
    *(struct addrinfo **)(uintptr_t)a[3] = ai;
out:
    *ret = (uint32_t)r;
    return true;
}

/* (ai): only an answer getaddrinfo() gave and not yet freed */
H(c_lwip_freeaddrinfo)
{
    if (ai_release((uintptr_t)a[0])) box_free_locked(w, (uintptr_t)a[0]);
    *ret = 0;
    return true;
}

static uint32_t addr_bytes(int af) { return af == AF_INET6 ? 16u : 4u; }

/* (af, src, dst, size) -> dst or NULL */
H(c_lwip_inet_ntop)
{
    uint8_t src[16];
    char out[STR_MAX];
    uint32_t r = 0;
    const uint32_t size = a[3] > STR_MAX ? STR_MAX : a[3];
    if ((a[0] == AF_INET || a[0] == AF_INET6) && ok_ptr(w, a[1], addr_bytes((int)a[0])) &&
        size && ok_ptr(w, a[2], size)) {
        memcpy(src, (const void *)(uintptr_t)a[1], addr_bytes((int)a[0]));
        if (lwip_inet_ntop((int)a[0], src, out, (socklen_t)size)) {
            memcpy((void *)(uintptr_t)a[2], out, strnlen(out, size - 1) + 1);
            ((char *)(uintptr_t)a[2])[size - 1] = '\0';
            r = a[2];
        } else {
            box_set_errno(w, errno);
        }
    } else {
        box_set_errno(w, a[0] == AF_INET || a[0] == AF_INET6 ? ENOSPC : EAFNOSUPPORT);
    }
    *ret = r;
    return true;
}

/* (af, src, dst) -> 1, 0 not an address, -1 */
H(c_lwip_inet_pton)
{
    const char *s = str(w, a[1], STR_MAX);
    uint8_t dst[16];
    int r;
    if ((a[0] != AF_INET && a[0] != AF_INET6)) r = fail(w, EAFNOSUPPORT);
    else if (!s || !ok_ptr(w, a[2], addr_bytes((int)a[0]))) r = fail(w, EFAULT);
    else {
        char copy[STR_MAX];
        strcpy(copy, s);
        r = lwip_inet_pton((int)a[0], copy, dst);
        if (r == 1) memcpy((void *)(uintptr_t)a[2], dst, addr_bytes((int)a[0]));
        else if (r < 0) r = fail(w, errno);
    }
    *ret = (uint32_t)r;
    return true;
}

H(c_ipaddr_addr)
{
    const char *s = str(w, a[0], STR_MAX);
    char copy[STR_MAX];
    if (s) strcpy(copy, s);
    *ret = s ? (uint32_t)ipaddr_addr(copy) : (uint32_t)IPADDR_NONE;
    return true;
}

/* ---- the routes -------------------------------------------------------- */

H(c_net_online)         { (void)w; (void)a; *ret = net_online(); return true; }
H(c_ethernet_connected) { (void)w; (void)a; *ret = ethernet_connected(); return true; }
H(c_ethernet_link)      { (void)w; (void)a; *ret = ethernet_link(); return true; }
H(c_wifi_up)            { (void)w; (void)a; *ret = wifi_up(); return true; }
H(c_wifi_connected)     { (void)w; (void)a; *ret = wifi_connected(); return true; }
H(c_wifi_ntp_synced)    { (void)w; (void)a; *ret = wifi_ntp_synced(); return true; }
H(c_wifistore_count)    { (void)w; (void)a; *ret = (uint32_t)wifistore_count(); return true; }

typedef bool (*text_fn)(char *, size_t);

/* (out, out_size): through the host's stack, so a short or unterminated
 * answer is still a string in the box; out_size 0 writes nothing. */
static uint32_t text_out(win_t *w, const uint32_t a[7], text_fn fn)
{
    char tmp[STR_MAX];
    const uint32_t size = a[1] > STR_MAX ? STR_MAX : a[1];
    if (!size) return 0;
    if (!ok_ptr(w, a[0], size)) return 0;
    tmp[0] = '\0';
    const bool r = fn(tmp, size);
    tmp[size - 1] = '\0';
    memcpy((void *)(uintptr_t)a[0], tmp, strlen(tmp) + 1);
    return r;
}

H(c_ethernet_ip)  { *ret = text_out(w, a, ethernet_ip); return true; }
H(c_wifi_sta_ip)  { *ret = text_out(w, a, wifi_sta_ip); return true; }
H(c_wifi_sta_ssid){ *ret = text_out(w, a, wifi_sta_ssid); return true; }

static bool route_text(char *out, size_t size) { net_route_describe(out, size); return true; }
H(c_net_route_describe) { (void)text_out(w, a, route_text); *ret = 0; return true; }

/* (timeout_ms, gw_ms *, net_ms *, what, what_size) */
H(c_net_probe)
{
    int gw = -1, net = -1;
    char what[STR_MAX];
    const uint32_t size = a[4] > STR_MAX ? STR_MAX : a[4];
    if ((a[1] && !ok_ptr(w, a[1], sizeof(int))) || (a[2] && !ok_ptr(w, a[2], sizeof(int))) ||
        (size && !ok_ptr(w, a[3], size))) {
        *ret = false;
        return true;
    }
    what[0] = '\0';
    const bool r = net_probe(a[0] > PROBE_MAX_MS ? PROBE_MAX_MS : a[0], &gw, &net, what, sizeof what);
    if (a[1]) *(int *)(uintptr_t)a[1] = gw;
    if (a[2]) *(int *)(uintptr_t)a[2] = net;
    if (size) {
        what[size - 1] = '\0';
        memcpy((void *)(uintptr_t)a[3], what, strlen(what) + 1);
    }
    *ret = r;
    return true;
}

const feckless_umode_call_t box_net_calls[] = {
    { "feckless_abi_network_v1", NULL, &s_network_v1, sizeof s_network_v1, 0 },
    FNU(lwip_socket), FNU(lwip_bind), FNU(lwip_connect), FNU(lwip_listen), FNU(lwip_accept),
    FNU(lwip_send), FNU(lwip_sendto), FNU(lwip_recv), FNU(lwip_recvfrom),
    FNU(lwip_setsockopt), FNU(lwip_getsockopt), FNU(lwip_getsockname), FNU(lwip_getpeername),
    FNU(lwip_shutdown), FNU(lwip_close), FNU(close), FNU(lwip_fcntl), FNU(select),
    FNU(lwip_getaddrinfo), FNU(lwip_freeaddrinfo),
    FNU(lwip_inet_ntop), FNU(lwip_inet_pton), FNU(ipaddr_addr),
    FNU(net_online), FNU(ethernet_connected), FNU(ethernet_link), FNU(ethernet_ip),
    FNU(wifi_up), FNU(wifi_connected), FNU(wifi_ntp_synced), FNU(wifi_sta_ip), FNU(wifi_sta_ssid),
    FNU(wifistore_count), FNU(net_route_describe), FNU(net_probe),
};
const uint32_t box_net_calls_n = sizeof box_net_calls / sizeof box_net_calls[0];

/* The app's sockets closed, its answers forgotten (the box goes with it). */
void box_net_end(void)
{
    for (int i = 0; i < SOCKS; i++) {
        int fd = -1;
        portENTER_CRITICAL(&s_mux);
        if (s_sock[i].used) {
            fd = s_sock[i].fd;
            s_sock[i].used = false;
        }
        portEXIT_CRITICAL(&s_mux);
        if (fd >= 0) lwip_close(fd);
    }
    portENTER_CRITICAL(&s_mux);
    memset(s_ai, 0, sizeof s_ai);
    portEXIT_CRITICAL(&s_mux);
}

#else  /* no network in this launcher */

const feckless_umode_call_t box_net_calls[1];
const uint32_t box_net_calls_n = 0;
void box_net_end(void) {}

#endif
