# Feckless libraries for M5Tab5

ESP-IDF components for the M5Stack Tab5 (ESP32-P4), in one repository
under one tag:

| Component | What it is |
|---|---|
| `feckless_drivers` | The USB-A host and its USB audio and HID class drivers, the battery monitor, the RX8130 clock, the touch controller (GT911 or ST7123, probed), audio out |
| `feckless_graphics_handler` | The ST7121 panel and backlight, and gfx: a rotating RGB565 framebuffer with a DMA2D blit, Ark Pixel 12px text with CJK, Galmuri 12px Hangul, Arabic shaping and right-to-left lines |
| `feckless_storage_handler` | The microSD slot and USB-A mass storage, mounted and watched, with one arbiter that leases the volume per read |
| `feckless_network_handler` | Wi-Fi through the ESP32-C6 (esp_hosted over SDIO2), a saved-network list in NVS, and USB Ethernet (ASIX, CDC-ECM) on the USB-A port, the wired link preferred when it can route |
| `feckless_camera` | The SC202CS camera on MIPI CSI, as luma frames, through esp_video; it takes the I2C bus from `feckless_drivers`' tab5io as an argument |
| `fatfs` | ESP-IDF's own fatfs with exFAT: it stands in for IDF's, and builds the installed IDF's FatFs from a copy patched at configure time |
| `usb_host_msc` | Espressif's USB mass storage class driver 1.3.0, with the Tab5's fixes (Apache-2.0) |
| `feckless_abi` | The ABI between a launcher and the apps it loads with esp_elf: the list of symbols an app's ELF may take from the launcher, and the build check |

Each is in `components/`, with its own README.

They were repositories of their own, each tagged on its own:
feckless-drivers-for-m5tab5, feckless-graphics-handler-for-m5tab5,
feckless-storage-handler-for-m5tab5 and feckless-abi-for-m5tab5 until
v0.1.0, feckless-network-handler-for-m5tab5 and feckless-camera-for-m5tab5
until v0.2.0, now archived. `ARCHITECTURE.md` says what each was imported at.

## Using them

Name each component the project uses, all at the same tag:

```yaml
# main/idf_component.yml
dependencies:
  feckless_drivers:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_drivers
    version: "v0.1.0"
  feckless_graphics_handler:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_graphics_handler
    version: "v0.1.0"
```

The components require each other by name, not by a git dependency, so
the project's manifest is the one place a tag is written. Which ones a
component needs:

- `feckless_abi`: `feckless_drivers` and `feckless_graphics_handler`.
- `feckless_storage_handler`: a `usb_host_msc` component, by name --
  `usb_host_msc` from here, Espressif's 1.3.0 with the Tab5's fixes
  (configure warns about any other). For
  exFAT cards and drives, `fatfs` from here as well -- IDF's own is
  FAT32 only.
- `feckless_network_handler`: `esp_usbh_asix`, from
  github.com/Sudrien/esp_usbh_asix at v0.1.0, which its manifest pins.
- `feckless_camera`: nothing by name; `camera_init()` takes the bus,
  and the panel's bring-up (`lcd_init()`) powers the MIPI PHY it shares,
  so it runs after that.
  On P4 silicon before v3 (the Tab5s so far), an `esp_ipa` the app
  supplies: the component's README says why.
- `feckless_drivers`, `feckless_graphics_handler`,
  `feckless_network_handler` and `feckless_camera`: nothing of each
  other's.

`feckless_abi` fails the build if a sibling it requires is at another
version than its own.

## Licence

MIT (`LICENSE`), except the font data in
`components/feckless_graphics_handler/fonts`: Ark Pixel and Galmuri are
OFL-1.1, Arabixel Basic is CC BY 4.0. That component's README says what
goes with a firmware built with them.
