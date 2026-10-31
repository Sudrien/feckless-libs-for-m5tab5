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

    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
