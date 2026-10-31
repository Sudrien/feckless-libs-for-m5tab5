# feckless_abi_nvs

The feckless ABI's nvs section: `feckless_nvs_handler`, for an app's
ELF, from a launcher that links this component. The list is
feckless_abi's `sections/nvs.txt`; feckless_abi's README says what a
section is.

## A launcher

Name `feckless_abi_nvs` and `feckless_nvs_handler` in the manifest, at
the tag of the rest, and `feckless_abi_nvs` in main's `REQUIRES`. Call
`nvsh_init()` at boot, before `feckless_abi_run()`; that registers the
section's table.

## An app

```yaml
  feckless_abi_nvs:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_abi_nvs
    version: "v0.4.0"
  feckless_nvs_handler:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_nvs_handler
    version: "v0.4.0"
```

beside `feckless_abi`, `feckless_drivers` and `feckless_graphics_handler`,
and `feckless_abi_nvs` in main's `REQUIRES`. Then:

```c
#include "feckless_abi.h"
#include "feckless_abi_nvs.h"   /* and nvs_handler.h */

void app_main(void)
{
    feckless_abi_require();
    feckless_abi_require_nvs();
    ...
```

The app finds NVS up, and does not call `nvsh_init()`, which is not
exported. It shares the partition with the launcher and every other
app: keep to a namespace of your own.

## Licence

MIT. See `LICENSE`.
