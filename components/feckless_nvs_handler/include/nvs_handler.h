/*
 * nvs_handler.h -- settings and saves in the Tab5's NVS partition, a key
 * at a time.
 *
 * ESP-IDF's NVS behind one call per value: each opens the namespace,
 * reads or writes, commits and closes. No handle crosses the API, so an
 * app's ELF can take these from a launcher (the feckless ABI's nvs
 * section, feckless_abi_nvs) with nothing laid out by either side's
 * build but plain integers and bytes.
 *
 * nvsh_init() brings the partition up, once, and is the launcher's (or a
 * firmware's own app_main()'s): an app on a launcher finds it up and
 * does not call it. Everything else may be called from any task once it
 * has; before it, or if it failed, each returns ESP_ERR_INVALID_STATE.
 *
 * A namespace is at most 15 characters, a key too (NVS_KEY_NAME_MAX_SIZE
 * less the NUL). The partition is shared by everything the board runs --
 * a launcher and each app it loads -- so an app keeps to a namespace of
 * its own.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the default NVS partition. If it is full or from a newer
 * NVS format, erase it and initialise it afresh, as IDF's examples do --
 * everything saved is lost, which is said in the log. Returns ESP_OK
 * when it is up, at once if it already was; otherwise IDF's error, and
 * nothing will be saved. Not exported to apps. */
esp_err_t nvsh_init(void);

/* True once nvsh_init() has succeeded. */
bool nvsh_ready(void);

/* A blob. *len is the room at buf on the way in and the blob's size on
 * the way out. ESP_ERR_NVS_NOT_FOUND when there is none;
 * ESP_ERR_NVS_INVALID_LENGTH when it is larger than *len, with *len its
 * size. buf may be NULL to ask the size. */
esp_err_t nvsh_get_blob(const char *ns, const char *key, void *buf, size_t *len);
esp_err_t nvsh_set_blob(const char *ns, const char *key, const void *buf, size_t len);

/* A NUL-terminated string; *len as for a blob, counting the NUL. */
esp_err_t nvsh_get_str(const char *ns, const char *key, char *buf, size_t *len);
esp_err_t nvsh_set_str(const char *ns, const char *key, const char *s);

/* Integers. On any error *out is left as it was, so a caller can fill it
 * with the default first. */
esp_err_t nvsh_get_u8(const char *ns, const char *key, uint8_t *out);
esp_err_t nvsh_set_u8(const char *ns, const char *key, uint8_t v);
esp_err_t nvsh_get_u32(const char *ns, const char *key, uint32_t *out);
esp_err_t nvsh_set_u32(const char *ns, const char *key, uint32_t v);
esp_err_t nvsh_get_i32(const char *ns, const char *key, int32_t *out);
esp_err_t nvsh_set_i32(const char *ns, const char *key, int32_t v);

/* Remove a key, or every key in the namespace. ESP_ERR_NVS_NOT_FOUND
 * when there was nothing to remove. */
esp_err_t nvsh_erase_key(const char *ns, const char *key);
esp_err_t nvsh_erase_ns(const char *ns);

/* A migration (7001): moving values from the namespaces an app or
 * firmware used to keep them in to the one it keeps them in now.
 *
 * Each entry names a key, the old namespace it lives in and its type.
 * The type is the key's own: nvsh reads a key as the type it is told,
 * and a key of another type is an error, not a guess. Plain integers
 * and pointers only, so an app's ELF can submit one through the nvs
 * section. */
typedef enum {
    NVSH_BLOB = 0,
    NVSH_STR,
    NVSH_U8,
    NVSH_U32,
    NVSH_I32,
} nvsh_type_t;

typedef struct {
    const char *ns;     /* the old namespace */
    const char *key;    /* the key in it, and in the new namespace */
    nvsh_type_t type;
} nvsh_move_t;

/* Move the listed values into `to_ns`, one old namespace at a time:
 *
 *  - a key the new namespace already has, of any type, is kept as it is
 *    (the new one is the newer write);
 *  - a key the old namespace lacks is skipped, so a device with nothing
 *    to move costs one failed read per key and writes nothing;
 *  - once every key of an old namespace is copied or kept, the whole old
 *    namespace is erased -- keys the list does not name go with it, so
 *    list everything that matters;
 *  - if a copy fails (the partition full, a key not of the type given)
 *    that namespace is left as it was, with whatever was copied already
 *    in the new one, and the call returns that error after trying the
 *    others. Called again at the next boot, it picks up where it was.
 *
 * *moved, if not NULL, is the number of values copied. ESP_OK when
 * nothing failed, nothing to move included. ESP_ERR_INVALID_STATE before
 * nvsh_init(); ESP_ERR_INVALID_ARG for a NULL name, or an old namespace
 * that is `to_ns`, before anything is touched. Each move is logged once
 * per old namespace, at warning level. */
esp_err_t nvsh_migrate(const char *to_ns, const nvsh_move_t *moves, size_t n,
                       size_t *moved);

/*
 * WHAT IS THERE (9005): every key in the partition, for finding out what
 * a setting became -- a key that seems to have gone, a partition filling
 * up. Read-only.
 *
 * An entry says its namespace, key, type ("u8", "i8", "u16", "i16",
 * "u32", "i32", "u64", "i64", "str", "blob"), its size in bytes (an
 * integer's width; a string's length with its NUL; a blob's length),
 * for an integer its value, and for a string or blob a 32-bit FNV-1a of
 * the bytes (which tells "the same as before" from "changed") and the
 * bytes themselves, with whether they are text: valid UTF-8 with no
 * control characters but tab and newline.
 *
 * THE BYTES ARE THE PARTITION'S, SECRETS INCLUDED: Wi-Fi passwords, mail
 * passwords, API secrets, the device's private key. They are given to
 * the callback only, valid only during the call, and wiped after it.
 * Plain chars, integers and a pointer, so an app's ELF can take entries
 * through the nvs section.
 */
typedef struct {
    char     ns[16];
    char     key[16];
    char     type[5];
    uint32_t size;
    int64_t  value;     /* an integer's (a u64's as its bits); else 0 */
    uint32_t hash;      /* a string's or blob's FNV-1a; else 0 */
    const uint8_t *data; /* a string's (without its NUL) or blob's bytes,
                          * during the callback only; else NULL */
    uint32_t len;       /* how many there are at data */
    bool     text;      /* data is UTF-8 text, as above */
} nvsh_entry_t;

/* Return false to stop. */
typedef bool (*nvsh_entry_cb_t)(const nvsh_entry_t *e, void *arg);

/* Each entry of namespace `ns`, or of every namespace when NULL, in NVS's
 * own order, to `cb` (which may be NULL, to count). The number given, or
 * -1 before nvsh_init() or when NVS could not be walked. */
int nvsh_list(const char *ns, nvsh_entry_cb_t cb, void *arg);

/* How full the partition is, in NVS's 32-byte entries: used, free (which
 * counts the page NVS keeps in reserve), available for data, and in all,
 * and how many namespaces it holds. A partition with no free page left
 * is the one nvsh_init() erases at the next boot. */
typedef struct {
    uint32_t used, free, available, total, namespaces;
} nvsh_stats_t;
esp_err_t nvsh_stats(nvsh_stats_t *out);

/* The stats, then every entry of `ns` (NULL: all), to the console at info
 * level, a line each: integers with their values, strings and blobs with
 * their sizes and hashes, and -- with `contents` -- those that are text
 * as text too, quoted and escaped, the first 160 bytes. With `contents`
 * the console shows every secret that is text; the log says so first. */
void nvsh_log(const char *ns, bool contents);

#ifdef __cplusplus
}
#endif
