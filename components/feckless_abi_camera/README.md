# feckless_abi_camera

The feckless ABI's camera section: `feckless_camera`, for an app's ELF,
from a launcher that links this component. The list is feckless_abi's
`sections/camera.txt`; feckless_abi's README says what a section is.

## A launcher

Name `feckless_abi_camera` and `feckless_camera` in the manifest, at the
tag of the rest, and `feckless_abi_camera` in main's `REQUIRES`. The
camera runs under the launcher's configuration and build, so the
launcher does what feckless_camera's README asks of an application:
the sensor and the ISP's controller in `sdkconfig.defaults` (configure
warns without them), and on silicon before v3, an `esp_ipa` of its own.
A tuning file, if the colours matter, is the launcher's too.

The launcher need not call `camera_init()`; the app does.

## An app

```yaml
  feckless_abi_camera:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_abi_camera
    version: "v0.4.0"
  feckless_camera:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_camera
    version: "v0.4.0"
```

beside `feckless_abi`, `feckless_drivers` and `feckless_graphics_handler`,
and `feckless_abi_camera` in main's `REQUIRES`. Then:

```c
#include "feckless_abi.h"
#include "feckless_abi_camera.h"   /* and camera.h */

void app_main(void)
{
    feckless_abi_require();
    feckless_abi_require_camera();
    ...
    camera_init(tab5io_bus());     /* once a boot; again returns ESP_OK */
```

The rest is feckless_camera's README: after `lcd_init()`, one task owns
the camera between `camera_start()` and `camera_stop()`. Stop it before
`app_main()` returns or the app's tasks end -- the launcher does not.

## Licence

MIT. See `LICENSE`.
