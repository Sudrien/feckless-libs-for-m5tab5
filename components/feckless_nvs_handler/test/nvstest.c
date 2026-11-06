/* Host test for nvsh_migrate(), against an in-memory NVS. SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "nvs_handler.h"
#include "nvs.h"
#include "esp_log.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static const nvsh_move_t K_MOVES[] = {
    { "old1", "blob", NVSH_BLOB }, { "old1", "str", NVSH_STR }, { "old1", "u8", NVSH_U8 },
    { "old1", "u32", NVSH_U32 },   { "old2", "i32", NVSH_I32 }, { "old2", "blob", NVSH_BLOB },
};
#define NM (sizeof K_MOVES / sizeof K_MOVES[0])

static void setup(void)
{
    fake_nvs_reset();
    CHECK(nvsh_init() == ESP_OK);
}

static void seed_old(void)
{
    const uint8_t b[5] = { 1, 2, 3, 4, 5 };
    CHECK(nvsh_set_blob("old1", "blob", b, 5) == ESP_OK);
    CHECK(nvsh_set_str("old1", "str", "hello") == ESP_OK);
    CHECK(nvsh_set_u8("old1", "u8", 7) == ESP_OK);
    CHECK(nvsh_set_u32("old1", "u32", 0xdeadbeef) == ESP_OK);
    CHECK(nvsh_set_i32("old2", "i32", -5) == ESP_OK);
    CHECK(nvsh_set_blob("old2", "blob", b, 3) == ESP_OK);
}

/* ---- 9005: nvsh_list(), nvsh_stats(), nvsh_log() ---------------------- */

static uint32_t fnv(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

/* What the callback saw, copied while the bytes were valid. */
static struct { char ns[16], key[16], type[5]; uint32_t size, len, hash; int64_t value; int text; char bytes[400]; } seen[32];
static int nseen;

static bool keep(const nvsh_entry_t *e, void *arg)
{
    if (nseen < 32) {
        strcpy(seen[nseen].ns, e->ns); strcpy(seen[nseen].key, e->key); strcpy(seen[nseen].type, e->type);
        seen[nseen].size = e->size; seen[nseen].len = e->len; seen[nseen].hash = e->hash;
        seen[nseen].value = e->value; seen[nseen].text = e->text;
        if (e->data && e->len < sizeof seen[0].bytes) memcpy(seen[nseen].bytes, e->data, e->len);
        nseen++;
    }
    return arg == NULL;         /* a non-NULL arg: stop after the first */
}

static int find_seen(const char *ns, const char *key)
{
    for (int i = 0; i < nseen; i++) if (!strcmp(seen[i].ns, ns) && !strcmp(seen[i].key, key)) return i;
    return -1;
}

/* Valid UTF-8, for checking what nvsh_log() cut. */
static int utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int k = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (k < 0) return 0;
        p++;
        while (k--) { if ((*p & 0xC0) != 0x80) return 0; p++; }
    }
    return 1;
}

static void list_tests(void)
{
    setup();
    const uint8_t bin[4] = { 0, 1, 2, 255 };
    CHECK(nvsh_set_str("defeatist", "pi_key", "abc123KEY") == ESP_OK);
    CHECK(nvsh_set_str("defeatist", "pi_sec", "S3cr3t!") == ESP_OK);
    CHECK(nvsh_set_blob("defeatist", "nets", bin, 4) == ESP_OK);
    CHECK(nvsh_set_u8("jinxed", "rotation", 1) == ESP_OK);
    CHECK(nvsh_set_blob("feckless_mail", "acct", "user@example.com", 16) == ESP_OK);
    CHECK(nvsh_set_str("other", "utf", "h\xc3\xa9llo \xe2\x80\x94 \xe6\x9d\xb1\xe4\xba\xac") == ESP_OK);
    CHECK(nvsh_set_str("other", "ctl", "a\x01" "b") == ESP_OK);
    CHECK(nvsh_set_i32("other", "neg", -5) == ESP_OK);
    CHECK(nvsh_set_str("other", "esc", "say \"hi\"\nok\\") == ESP_OK);
    CHECK(nvsh_set_blob("other", "bad8", "\xc3\x28", 2) == ESP_OK);    /* not UTF-8 */

    nseen = 0;
    CHECK(nvsh_list(NULL, keep, NULL) == 10);
    CHECK(fake_nvs_live_iterators() == 0);
    int i = find_seen("defeatist", "pi_key");
    CHECK(i >= 0 && !strcmp(seen[i].type, "str") && seen[i].size == 10 && seen[i].len == 9 &&
          seen[i].text && !memcmp(seen[i].bytes, "abc123KEY", 9) && seen[i].hash == fnv("abc123KEY", 9));
    i = find_seen("defeatist", "nets");
    CHECK(i >= 0 && !strcmp(seen[i].type, "blob") && seen[i].size == 4 && !seen[i].text &&
          seen[i].hash == fnv(bin, 4) && !memcmp(seen[i].bytes, bin, 4));
    i = find_seen("jinxed", "rotation");
    CHECK(i >= 0 && !strcmp(seen[i].type, "u8") && seen[i].size == 1 && seen[i].value == 1 && seen[i].hash == 0);
    i = find_seen("other", "neg");
    CHECK(i >= 0 && !strcmp(seen[i].type, "i32") && seen[i].value == -5);
    i = find_seen("feckless_mail", "acct");
    CHECK(i >= 0 && seen[i].text);
    CHECK((i = find_seen("other", "utf")) >= 0 && seen[i].text);
    CHECK((i = find_seen("other", "ctl")) >= 0 && !seen[i].text);
    CHECK((i = find_seen("other", "bad8")) >= 0 && !seen[i].text);

    /* One namespace; none; stopping early. */
    nseen = 0;
    CHECK(nvsh_list("jinxed", keep, NULL) == 1 && nseen == 1);
    CHECK(nvsh_list("nothing", keep, NULL) == 0);
    nseen = 0;
    static int stop = 1;
    CHECK(nvsh_list(NULL, keep, &stop) == 1 && nseen == 1);
    CHECK(fake_nvs_live_iterators() == 0);
    CHECK(nvsh_list(NULL, NULL, NULL) == 10);

    nvsh_stats_t st;
    CHECK(nvsh_stats(&st) == ESP_OK && st.namespaces == 4 && st.used > 0 && st.used + st.free == st.total);
    CHECK(nvsh_stats(NULL) == ESP_ERR_INVALID_ARG);

    /* Without contents: names, sizes and hashes; not one secret. */
    fake_log_text[0] = '\0';
    nvsh_log(NULL, false);
    CHECK(strstr(fake_log_text, "pi_key") && strstr(fake_log_text, "bytes, #"));
    CHECK(strstr(fake_log_text, "rotation") && strstr(fake_log_text, " 1\n"));
    CHECK(strstr(fake_log_text, "-5\n"));
    CHECK(!strstr(fake_log_text, "S3cr3t") && !strstr(fake_log_text, "abc123KEY") &&
          !strstr(fake_log_text, "user@example.com"));
    CHECK(strstr(fake_log_text, "4 namespaces") && strstr(fake_log_text, "10 key(s)"));

    /* With contents: text as text, escaped; binary and controls never. */
    fake_log_text[0] = '\0';
    nvsh_log(NULL, true);
    CHECK(strstr(fake_log_text, "\"S3cr3t!\"") && strstr(fake_log_text, "\"abc123KEY\""));
    CHECK(strstr(fake_log_text, "\"user@example.com\""));
    CHECK(strstr(fake_log_text, "\xe6\x9d\xb1\xe4\xba\xac\""));
    CHECK(strstr(fake_log_text, "\"say \\\"hi\\\"\\nok\\\\\""));
    CHECK(!strstr(fake_log_text, "a\x01"));
    CHECK(!strstr(fake_log_text, "\xc3\x28"));
    CHECK(utf8_ok(fake_log_text));

    /* A long string: its first 160 bytes at most, cut on a character
     * boundary, and "..." after; a long ASCII one the same. */
    char longs[400] = "";
    for (int k = 0; k < 100; k++) strcat(longs, "\xe6\x9d\xb1");     /* 300 bytes, 3 a character */
    CHECK(nvsh_set_str("other", "long", longs) == ESP_OK);
    char longa[301];
    memset(longa, 'x', 300); longa[300] = '\0';
    CHECK(nvsh_set_str("other", "longa", longa) == ESP_OK);
    fake_log_text[0] = '\0';
    nvsh_log("other", true);
    CHECK(utf8_ok(fake_log_text));
    const char *l = strstr(fake_log_text, "long ");
    const char *q = l ? strchr(l, '"') : NULL;
    const char *qe = q ? strchr(q + 1, '"') : NULL;
    CHECK(q && qe && qe - q - 1 <= 160 && (qe - q - 1) % 3 == 0 && !strncmp(qe, "\"...", 4));
    l = strstr(fake_log_text, "longa ");
    q = l ? strchr(l, '"') : NULL;
    qe = q ? strchr(q + 1, '"') : NULL;
    CHECK(q && qe && qe - q - 1 == 160 && !strncmp(qe, "\"...", 4));
    CHECK(fake_nvs_live_iterators() == 0);
}

int main(void)
{
    size_t moved;

    /* before nvsh_init(): refused */
    fake_nvs_reset();
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_ERR_INVALID_STATE);

    /* nothing to move: ESP_OK, nothing written */
    setup();
    int w0 = fake_nvs_writes();
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_OK);
    CHECK(moved == 0 && fake_nvs_writes() == w0 && !fake_nvs_has_ns("new"));

    /* the whole move */
    setup(); seed_old();
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_OK);
    CHECK(moved == 5);                  /* old2's blob: old1's is there first, kept */
    {
        uint8_t b[8]; size_t l = sizeof b; char s[16]; size_t sl = sizeof s;
        uint8_t u8 = 0; uint32_t u32 = 0; int32_t i32 = 0;
        CHECK(nvsh_get_blob("new", "blob", b, &l) == ESP_OK && l == 5 && b[4] == 5);   /* old1's: first listed wins */
        CHECK(nvsh_get_str("new", "str", s, &sl) == ESP_OK && !strcmp(s, "hello") && sl == 6);
        CHECK(nvsh_get_u8("new", "u8", &u8) == ESP_OK && u8 == 7);
        CHECK(nvsh_get_u32("new", "u32", &u32) == ESP_OK && u32 == 0xdeadbeef);
        CHECK(nvsh_get_i32("new", "i32", &i32) == ESP_OK && i32 == -5);
    }
    CHECK(!fake_nvs_has_ns("old1") && !fake_nvs_has_ns("old2"));
    CHECK(fake_log_warn == 2 && fake_log_err == 0);

    /* run again: nothing left, nothing written */
    w0 = fake_nvs_writes();
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_OK && moved == 0);
    CHECK(fake_nvs_writes() == w0);

    /* a key the new namespace has is kept, whatever its type; old ns still erased */
    setup(); seed_old();
    CHECK(nvsh_set_u8("new", "u8", 99) == ESP_OK);
    CHECK(nvsh_set_str("new", "u32", "other type") == ESP_OK);          /* mismatch counts as there */
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_OK);
    {
        uint8_t u8 = 0; char s[16]; size_t sl = sizeof s;
        CHECK(nvsh_get_u8("new", "u8", &u8) == ESP_OK && u8 == 99);
        CHECK(nvsh_get_str("new", "u32", s, &sl) == ESP_OK && !strcmp(s, "other type"));
    }
    CHECK(moved == 3 && !fake_nvs_has_ns("old1"));   /* blob, str, i32 */

    /* a copy that fails leaves that namespace, and the others still go */
    setup(); seed_old();
    fake_nvs_fail_writes_after(2);               /* old1's third copy fails */
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) != ESP_OK);
    fake_nvs_fail_writes_after(-1);
    CHECK(fake_nvs_has_ns("old1"));              /* left in place */
    CHECK(fake_log_err >= 1);
    /* the next boot finishes it */
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_OK);
    CHECK(!fake_nvs_has_ns("old1") && !fake_nvs_has_ns("old2"));
    {
        uint32_t u32 = 0;
        CHECK(nvsh_get_u32("new", "u32", &u32) == ESP_OK && u32 == 0xdeadbeef);
    }

    /* a key of another type than declared: an error, the namespace left, the rest go */
    setup();
    CHECK(nvsh_set_str("old1", "u8", "not a u8") == ESP_OK);
    CHECK(nvsh_set_i32("old2", "i32", 1) == ESP_OK);
    CHECK(nvsh_migrate("new", K_MOVES, NM, &moved) == ESP_ERR_NVS_TYPE_MISMATCH);
    CHECK(fake_nvs_has_ns("old1") && !fake_nvs_has_ns("old2"));

    /* bad arguments, before anything is touched */
    setup(); seed_old();
    {
        const nvsh_move_t same[] = { { "new", "x", NVSH_U8 } };
        const nvsh_move_t nul[]  = { { NULL, "x", NVSH_U8 } };
        w0 = fake_nvs_writes();
        CHECK(nvsh_migrate(NULL, K_MOVES, NM, &moved) == ESP_ERR_INVALID_ARG);
        CHECK(nvsh_migrate("new", NULL, NM, &moved) == ESP_ERR_INVALID_ARG);
        CHECK(nvsh_migrate("new", same, 1, &moved) == ESP_ERR_INVALID_ARG);
        CHECK(nvsh_migrate("new", nul, 1, &moved) == ESP_ERR_INVALID_ARG);
        CHECK(nvsh_migrate("new", NULL, 0, NULL) == ESP_OK);
        CHECK(fake_nvs_writes() == w0 && fake_nvs_has_ns("old1"));
    }

    list_tests();

    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
