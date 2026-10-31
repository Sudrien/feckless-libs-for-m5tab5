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

Each call opens and closes the namespace: a handful of values at a time
is what it is for. A namespace and a key are at most 15 characters.
The partition is shared by everything the board runs, a launcher and
every app it loads, so each keeps to a namespace of its own.

## On a launcher

The launcher calls `nvsh_init()`; an app finds NVS up and does not call
it -- the nvs section exports everything here but `nvsh_init()`. A
firmware that runs an app's ELF itself, as a standalone build does,
calls `nvsh_init()` before `feckless_abi_run()`, as a launcher would.

## Licence

MIT. See `LICENSE`.
