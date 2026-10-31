# Feckless ABI for M5Tab5

The ABI between a launcher on the M5Stack Tab5 and the apps it loads
with esp_elf (Espressif's
[elf_loader](https://github.com/espressif/esp-iot-solution/tree/master/components/elf_loader)),
as an ESP-IDF component.

The launcher links the libraries, pinned here:
[feckless-drivers](https://github.com/Sudrien/feckless-drivers-for-m5tab5)
and
[feckless-graphics-handler](https://github.com/Sudrien/feckless-graphics-handler-for-m5tab5).
An app is an ELF that contains only its own code and what it links
statically, and leaves every call into the libraries, FreeRTOS, the heap,
the log, libc, libm and libgcc for the launcher to supply.
`exports.txt` is the list of what it may leave.

## Version 1

- **feckless_drivers v0.5.10 and feckless_graphics_handler v0.2.9**,
  their whole public API: every global their public headers declare.
  That is 110 functions and the font tables.
- **A fixed set from ESP-IDF and the toolchain**: FreeRTOS tasks,
  queues, semaphores and event groups; the heap; esp_timer; the log;
  esp_err; libc's strings, formatting and sorting; libm, float and
  double; libgcc's double-precision and 64-bit helpers.

Nothing else. File I/O and feckless-storage-handler are not in v1.

**The board may already be up.** A launcher runs an app on the board it
was using, from its own task: no restart. The app initialises the board
as a firmware of its own would, and the board's state is what that
leaves:

- `tab5io_init()`, `lcd_init()`, `gfx_init()`, `touch_init()` and
  `usbhost_init()` return `ESP_OK` when called again. The panel is kept,
  and gfx and touch go back to upright, full brightness and a black
  screen. `usbhost_start()` always could be called again.
- A USB class registered once the port is up, such as `uac_init()`, is
  installed, and the port's power is cycled so that devices already
  attached enumerate again and meet it. The launcher's own classes, mass
  storage for one, see their devices drop and return.
- `audio_out_init()`, `uac_init()`, `hid_init()`, `battery_init()` and
  `rtc8130_init()` are the app's. A launcher does not call them.

What else the launcher left running, a console task or a mounted card,
keeps running.

## An app

```yaml
# main/idf_component.yml
dependencies:
  feckless_abi:
    git: https://github.com/Sudrien/feckless-abi-for-m5tab5.git
    version: "v0.2.5"
```

`feckless_abi` goes in main's `REQUIRES`, and `feckless_abi_elf()` goes
in the project's CMakeLists.txt in place of elf_loader's `project_elf()`:

```cmake
project(myapp)
set(ELF_COMPONENTS ...)     # anything to link into the ELF, e.g. esp-dsp
feckless_abi_elf(myapp)     # writes build/myapp.app.elf
```

The first thing in `app_main()`:

```c
#include "feckless_abi.h"

void app_main(void)
{
    feckless_abi_require();
    ...
```

The ELF links IDF's `esp_app_format` too, so `esp_app_get_description()`
in an app describes the app: its project name, version and build time.

The build checks the ELF, and deletes it if the check fails, so a failed
build leaves no ELF to copy. It fails on any undefined symbol that is not
in `exports.txt`: link that one into the ELF with `ELF_COMPONENTS`, or
it needs a new ABI version. It also fails without
`feckless_abi_require()`.

Lothesome Audio Analyzer is an app built this way, and also builds a
standalone firmware that embeds its own ELF and loads it through this
component:
[lothesome-audio-analyzer-for-m5tab5](https://github.com/Sudrien/lothesome-audio-analyzer-for-m5tab5).

## A launcher

`feckless_abi` goes in main's `REQUIRES`, and then:

```c
#include "feckless_abi.h"

int err = feckless_abi_run(image, size);   /* runs the app's app_main() on this task */
```

`image` may be in RAM or memory-mapped flash, and must stay there while
the app runs. The app runs on the calling task, so give that task the
stack an app expects of `app_main()`: `CONFIG_ESP_MAIN_TASK_STACK_SIZE`
if it is the main task. A failure to load returns with nothing of the
app run, so the launcher can carry on. A return of 0 means the app's
`app_main()` returned, leaving its own tasks running; the launcher must
leave the screen alone from then on (`vTaskDelete(NULL)`). See
"Configuration" for the sdkconfig.

## Configuration

An app is compiled with its own sdkconfig and runs under its launcher's,
so the two must agree where the headers depend on it. The component
checks both builds, and fails on:

- `CONFIG_FREERTOS_HZ=1000`. Every `pdMS_TO_TICKS()` in an app is
  compiled with it.
- `CONFIG_SPIRAM=y`, `CONFIG_ELF_LOADER_LOAD_PSRAM=y` and
  `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n`. esp_elf runs apps from PSRAM.

It warns without:

- `CONFIG_CACHE_L2_CACHE_256KB=y` (and `..._LINE_128B=y`). The app's code
  is in PSRAM.
- `CONFIG_UAC_FREQ_NUM_MAX=8`, on the launcher. This one is compiled into
  the launcher's USB audio driver, not the app.

## Versions

`feckless_abi.h` defines `FECKLESS_ABI_VERSION` and a symbol named for
it, `feckless_abi_v1`. The launcher defines the symbol; every app refers
to it. An app built for another version fails in `esp_elf_relocate()`,
and esp_elf logs `feckless_abi_v1` (or whichever) as the symbol it could
not find. It fails before any of the app runs.

- **Adding** a name to `exports.txt` is compatible. Apps built against
  the shorter list still load; a new app that uses the name needs a
  launcher that has it. That is a minor release.
- **Removing** a name, or changing what one means, is a new version:
  a signature, or a struct. A feckless tag moved in `idf_component.yml`
  is a new version if it changes either. A tag that only changes
  behaviour is not; v0.1.0's v0.5.9/v0.2.7 to v0.5.10/v0.2.9, whose
  inits may be called twice, is not. Bump `FECKLESS_ABI_VERSION` and
  `FECKLESS_ABI_SYMBOL` together, in a major release.

After moving a tag, `tools/feckless_exports.py <build> <managed_components>`
prints the libraries' public API afresh, for the feckless section of
`exports.txt`.

## Licence

MIT. See `LICENSE`.
