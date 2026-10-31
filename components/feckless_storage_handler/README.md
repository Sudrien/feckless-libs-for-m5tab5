# Feckless Storage Handler for Tab5

The M5Stack Tab5's microSD slot and a USB drive on its USB-A port, as an
ESP-IDF component, and the arbiter that decides who reads from them.
Pulled out of
[Defeatist Music Player for M5Tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5),
where it was written and where it runs; the long comments in the source
are that project's record of why each thing is the way it is, and they
came along unchanged.

| Header | What it is |
|---|---|
| `storage.h` | Both volumes: mount, unmount, a poll task that notices cards and drives coming and going, a generation counter, holds that keep a volume mounted while something reads it, USB power that refuses to drop a volume in use |
| `storage_io.h` | One lease on the device, taken per read and granted by class (playback, prefetch, background), with reads cut into chunks so the highest class never waits for more than one |

## Using it

```yaml
# main/idf_component.yml
dependencies:
  feckless_storage_handler:
    git: https://github.com/Sudrien/feckless-storage-handler-for-m5tab5.git
    version: "v0.1.0"
```

and `feckless_storage_handler` in `main`'s `REQUIRES`.

```c
static const storage_usb_t usb = {
    .register_class = usbhost_register_class,   /* required */
    .set_power      = usbhost_set_power,
    .powered        = usbhost_powered,
};

usbhost_init(exp2);           /* yours: whatever owns the USB host */
storage_io_init();
storage_init(&usb);           /* before your USB host starts */
```

## What the application must do

**Own the USB host.** Mass storage is one class driver among several on
the Tab5's port, so this component registers its class through
`register_class` and does not install the host stack or switch USB5V_EN.

**Supply `usb_host_msc`.** Required by name rather than pulled from the
registry, because the player builds against a vendored 1.3.0 with fixes
the registry did not have. Anything else adds
`espressif/usb_host_msc` to its own manifest.

**Decide on exFAT.** With FatFs's exFAT enabled a file can be over 4 GB,
and `fseek()` takes `long`, which is 32 bits on this target.
`storage_io_read_at()` takes an `int64_t` offset and refuses, with a log
line, anything past `LONG_MAX` rather than seek to a truncated one; the
comment above that check says where a 64-bit seek would have to come
from (FatFs's `f_lseek`, not `fseeko`, since IDF's `off_t` is `long`
too). The player enables exFAT by vendoring a patched fatfs before
`project.cmake` runs, which a component cannot do for its application;
see its `cmake/exfat.cmake`.

## Licence

MIT. See `LICENSE`.
