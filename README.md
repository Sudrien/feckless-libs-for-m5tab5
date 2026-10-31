# Feckless libraries for M5Tab5

ESP-IDF components for the M5Stack Tab5 (ESP32-P4), in one repository
under one tag:

| Component | What it is |
|---|---|
| `feckless_drivers` | The USB-A host and its USB audio and HID class drivers, the battery monitor, the RX8130 clock, the touch controller (GT911 or ST7123, probed), audio out |
| `feckless_graphics_handler` | The ST7121 panel and backlight, and gfx: a rotating RGB565 framebuffer with a DMA2D blit, Ark Pixel 12px text with CJK, Galmuri 12px Hangul, Arabic shaping and right-to-left lines |
| `feckless_storage_handler` | The microSD slot and USB-A mass storage, mounted and watched, with one arbiter that leases the volume per read |
| `feckless_abi` | The ABI between a launcher and the apps it loads with esp_elf: the list of symbols an app's ELF may take from the launcher, and the build check |

Each is in `components/`, with its own README.

They were four repositories, each tagged on its own, until v1.0.0:
feckless-drivers-for-m5tab5, feckless-graphics-handler-for-m5tab5,
feckless-storage-handler-for-m5tab5 and feckless-abi-for-m5tab5, now
archived. `ARCHITECTURE.md` says what each was imported at.

## Using them

Name each component the project uses, all at the same tag:

```yaml
# main/idf_component.yml
dependencies:
  feckless_drivers:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_drivers
    version: "v1.0.0"
  feckless_graphics_handler:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_graphics_handler
    version: "v1.0.0"
```

The components require each other by name, not by a git dependency, so
the project's manifest is the one place a tag is written. Which ones a
component needs:

- `feckless_abi`: `feckless_drivers` and `feckless_graphics_handler`.
- `feckless_storage_handler`: a `usb_host_msc` component, by name. The
  Tab5 projects carry a patched 1.3.0 in their own `components/`.
- `feckless_drivers` and `feckless_graphics_handler`: nothing of each
  other's.

`feckless_abi` fails the build if a sibling it requires is at another
version than its own.

## Licence

MIT (`LICENSE`), except the font data in
`components/feckless_graphics_handler/fonts`: Ark Pixel and Galmuri are
OFL-1.1, Arabixel Basic is CC BY 4.0. That component's README says what
goes with a firmware built with them.
