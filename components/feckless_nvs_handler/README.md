# feckless_nvs_handler

The M5Stack Tab5's NVS partition, a value at a time, as an ESP-IDF
component. ESP-IDF's NVS behind one call per value -- open the
namespace, read or write, commit, close -- so no `nvs_handle_t` crosses
the API. That is what lets the feckless ABI's nvs section
(`feckless_abi_nvs`) give it to an app's ELF.

## Using it

```yaml
# main/idf_component.yml
dependencies:
  feckless_nvs_handler:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_nvs_handler
    version: "v0.4.0"
```

and `feckless_nvs_handler` in main's `REQUIRES`. Then:

```c
#include "nvs_handler.h"

nvsh_init();                                /* once, at boot */

uint8_t lang = 0;                           /* the default ... */
nvsh_get_u8("myapp", "lang", &lang);        /* ... kept if there is no key */
nvsh_set_u8("myapp", "lang", 2);

my_save_t s;
size_t len = sizeof s;
if (nvsh_get_blob("myapp", "save", &s, &len) == ESP_OK && len == sizeof s) { ... }
nvsh_set_blob("myapp", "save", &s, sizeof s);
```

`nvsh_init()` initialises the default partition, and if it is full or
from a newer NVS format erases it and starts again, as IDF's examples
do. A second call returns `ESP_OK` and does nothing. Before it, or if it
failed, every other call returns `ESP_ERR_INVALID_STATE`, so a board
whose NVS will not come up runs, and saves nothing.

**What is there** (9005). `nvsh_log(NULL, false)` writes how full the
partition is and every key in it to the console, a line each: integers
with their values, strings and blobs with their sizes and an FNV-1a
hash. `nvsh_log(NULL, true)` adds the contents of those that are text
(UTF-8, no control characters but tab and newline), quoted, escaped and
cut at 160 bytes -- **secrets included**, with a warning line first.
`nvsh_list()` hands each entry, bytes and all, to a callback;
`nvsh_stats()` is NVS's own count of used and free entries. A
namespace instead of NULL narrows either to it.

```
NVS: 20 of 504 entries used, 484 free (358 for data), 4 namespaces
  defeatist       pi_key          str  10 bytes, #68b9c60c "abc123KEY"
  defeatist       nets            blob 4 bytes, #6fab6075
  jinxed          rotation        u8   1
NVS: 8 key(s)
```

Each call opens and closes the namespace: a handful of values at a time
is what it is for. A namespace and a key are at most 15 characters.
The partition is shared by everything the board runs, a launcher and
every app it loads, so each keeps to a namespace of its own.

## Moving values from old namespaces (7001)

An app that renames its namespace, or folds several into one, submits
the move instead of writing the copy-and-erase itself:

```c
static const nvsh_move_t old[] = {
    { "radiokeep", "last",    NVSH_BLOB },
    { "radiokeep", "wifi_on", NVSH_U8   },
    { "devcert",   "key",     NVSH_STR  },
};
nvsh_migrate("myapp", old, sizeof old / sizeof old[0], NULL);
```

Each old namespace is handled whole: a key the new namespace already has
is kept (the new one is the newer write); a key the old one lacks is
skipped; once every listed key is copied or kept, **the old namespace is
erased, listed keys or not**. A copy that fails (the partition full, a
key of another type than the list says) leaves that namespace as it was,
and the same call at the next boot carries on. It is cheap when there is
nothing to move: one failed read per key, no writes. If two old
namespaces hold the same key, the one listed first wins. Nothing here
reads a value as a type it is not, and there is no i64 or 16-bit type:
the types are blob, string, u8, u32 and i32.

`make -C test` runs it against an in-memory NVS.

## On a launcher

The launcher calls `nvsh_init()`; an app finds NVS up and does not call
it -- the nvs section exports everything here but `nvsh_init()`. A
firmware that runs an app's ELF itself, as a standalone build does,
calls `nvsh_init()` before `feckless_abi_run()`, as a launcher would.

## Licence

MIT. See `LICENSE`.
