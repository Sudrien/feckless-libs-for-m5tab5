/*
 * redirecttest.c -- feckless_redirect.c on a host.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "feckless_redirect.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void want(const char *host, const char *uri, uint16_t port, const char *expect)
{
    char out[160];
    const bool ok = feckless_redirect_target(host, uri, "Feckless-A1B2C3", port, out, sizeof out);
    checks++;
    if (!ok || strcmp(out, expect) != 0) {
        fails++;
        printf("FAIL host=%s uri=%s port=%u: got \"%s\", want \"%s\"\n",
               host ? host : "(null)", uri ? uri : "(null)", (unsigned)port, out, expect);
    }
}

int main(void)
{
    /* An address goes to the name. */
    want("192.168.1.20",       "/",          443, "https://feckless-a1b2c3.local/");
    want("192.168.1.20:80",    "/x?y=1",     443, "https://feckless-a1b2c3.local/x?y=1");
    want("10.0.0.1",           "/a/b",       8443, "https://feckless-a1b2c3.local:8443/a/b");
    want("[fe80::1]",          "/",          443, "https://feckless-a1b2c3.local/");
    want("[fe80::1]:80",       "/",          443, "https://feckless-a1b2c3.local/");
    want(NULL,                 "/",          443, "https://feckless-a1b2c3.local/");
    want("",                   "/",          443, "https://feckless-a1b2c3.local/");

    /* A name is kept, with its case. */
    want("feckless-a1b2c3.local", "/",       443, "https://feckless-a1b2c3.local/");
    want("Player.Home.Arpa:80",   "/p",      443, "https://Player.Home.Arpa/p");
    want("router-name",           "/",       8443, "https://router-name:8443/");

    /* Not a host: treated as no host, never copied into the header. */
    want("evil.com\r\nSet-Cookie: x=1", "/", 443, "https://feckless-a1b2c3.local/");
    want("a b",                "/",          443, "https://feckless-a1b2c3.local/");
    want("host/evil",          "/",          443, "https://feckless-a1b2c3.local/");
    want("user@host",          "/",          443, "https://feckless-a1b2c3.local/");

    /* The path: kept, or "/". */
    want("1.2.3.4", "",        443, "https://feckless-a1b2c3.local/");
    want("1.2.3.4", "noslash", 443, "https://feckless-a1b2c3.local/");
    want("1.2.3.4", NULL,      443, "https://feckless-a1b2c3.local/");
    want("1.2.3.4", "/a b",    443, "https://feckless-a1b2c3.local/");
    want("1.2.3.4", "/a\r\nX: y", 443, "https://feckless-a1b2c3.local/");
    want("1.2.3.4", "/a\x7f",  443, "https://feckless-a1b2c3.local/");

    /* A name of 63 characters is kept; 64 is not a name here. */
    char h63[64], h64[65], e63[160];
    memset(h63, 'a', 63); h63[63] = '\0';
    memset(h64, 'a', 64); h64[64] = '\0';
    snprintf(e63, sizeof e63, "https://%s/", h63);
    want(h63, "/", 443, e63);
    want(h64, "/", 443, "https://feckless-a1b2c3.local/");

    /* Too long a path is "/", not cut: a cut path is a different page. */
    char longuri[300];
    longuri[0] = '/';
    memset(longuri + 1, 'p', sizeof longuri - 2);
    longuri[sizeof longuri - 1] = '\0';
    want("1.2.3.4", longuri, 443, "https://feckless-a1b2c3.local/");

    /* Bad arguments and small buffers. */
    char out[160] = "x";
    CHECK(!feckless_redirect_target("1.2.3.4", "/", NULL, 443, out, sizeof out) && !out[0]);
    CHECK(!feckless_redirect_target("1.2.3.4", "/", "", 443, out, sizeof out) && !out[0]);
    CHECK(!feckless_redirect_target("1.2.3.4", "/", "n", 443, NULL, 10));
    CHECK(!feckless_redirect_target("1.2.3.4", "/", "n", 443, out, 0));
    char small[12];
    CHECK(!feckless_redirect_target("1.2.3.4", "/", "n", 443, small, sizeof small) && !small[0]);
    char exact[32];   /* "https://n.local/" is 16 characters */
    CHECK(feckless_redirect_target("1.2.3.4", "/", "n", 443, exact, 17) &&
          strcmp(exact, "https://n.local/") == 0);
    CHECK(!feckless_redirect_target("1.2.3.4", "/", "n", 443, exact, 16) && !exact[0]);
    CHECK(feckless_redirect_target("1.2.3.4", "/", "N", 443, exact, sizeof exact) &&
          strcmp(exact, "https://n.local/") == 0);

    /* feckless_dns_label */
    char lb[64];
    CHECK(feckless_dns_label("Feckless-A1B2C3", lb, sizeof lb) && strcmp(lb, "feckless-a1b2c3") == 0);
    CHECK(feckless_dns_label("My Tab5!", lb, sizeof lb) && strcmp(lb, "my-tab5") == 0);
    CHECK(feckless_dns_label("  --a__b  c--", lb, sizeof lb) && strcmp(lb, "a-b-c") == 0);
    CHECK(feckless_dns_label("a.b", lb, sizeof lb) && strcmp(lb, "a-b") == 0);
    CHECK(feckless_dns_label("\xc3\xa9t\xc3\xa9", lb, sizeof lb) && strcmp(lb, "t") == 0);   /* UTF-8 bytes are not letters */
    CHECK(!feckless_dns_label("---", lb, sizeof lb) && !lb[0]);
    CHECK(!feckless_dns_label("", lb, sizeof lb) && !lb[0]);
    CHECK(!feckless_dns_label(NULL, lb, sizeof lb) && !lb[0]);
    CHECK(!feckless_dns_label("a", NULL, 5));
    CHECK(!feckless_dns_label("a", lb, 0));
    {
        char big[200], o[200];
        memset(big, 'z', sizeof big - 1); big[sizeof big - 1] = '\0';
        CHECK(feckless_dns_label(big, o, sizeof o) && strlen(o) == 63);
        char small[4];
        CHECK(feckless_dns_label("abcdefgh", small, sizeof small) && strcmp(small, "abc") == 0);
        CHECK(feckless_dns_label("ab-cd", small, sizeof small) && strcmp(small, "ab") == 0);   /* no dangling '-' */
        char one[1];
        CHECK(!feckless_dns_label("abc", one, sizeof one) && !one[0]);
    }

    /* is_address */
    CHECK(feckless_redirect_is_address("1.2.3.4"));
    CHECK(feckless_redirect_is_address("1.2.3.4:80"));
    CHECK(feckless_redirect_is_address("[::1]"));
    CHECK(feckless_redirect_is_address(NULL));
    CHECK(feckless_redirect_is_address(""));
    CHECK(feckless_redirect_is_address("bad host"));
    CHECK(!feckless_redirect_is_address("feckless.local"));
    CHECK(!feckless_redirect_is_address("1a.example"));       /* a letter: a name */
    CHECK(!feckless_redirect_is_address("router:80"));

    printf("redirecttest: %d checks, %d failed\n", checks, fails);
    return fails;
}
