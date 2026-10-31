# Feckless Camera for Tab5

The M5Stack Tab5's camera, as grey frames, as an ESP-IDF component.
Written for [Unprofitable Mahjong](https://github.com/Sudrien/unprofitable-mahjong),
whose Scan a code reads replay QR codes with it, and run there on a
Tab5 (silicon v1.3, ESP-IDF 6.1). Beside
[feckless-drivers](https://github.com/Sudrien/feckless-drivers-for-m5tab5),
whose I2C bus it uses, and
[feckless-graphics-handler](https://github.com/Sudrien/feckless-graphics-handler-for-m5tab5),
whose panel bring-up powers the MIPI PHY it shares.

| | |
|---|---|
| Sensor | SmartSens SC202CS (SC2356), MIPI CSI, 1 lane |
| Mode | 1280x720 RAW8 30 fps (the Kconfig default), ISP to UYVY |
| Out | the Y of each frame, whole or at every n-th pixel |
| Bus | SCCB on tab5io's I2C, 400 kHz |
| Reset | CAM_RST, expander 0x43 P6, released by `tab5io_init()` |
| PHY supply | LDO 3 at 2.5 V, held by `lcd_init()` |

## Using it

```yaml
# main/idf_component.yml
dependencies:
  feckless_camera:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_camera
    version: "v0.2.0"
```

and `feckless_camera` in `main`'s `REQUIRES`. Then:

```c
tab5io_init();
lcd_init(&panel);
if (camera_init(tab5io_bus()) == ESP_OK) {      /* once, at boot: probes the sensor */
    camera_start();                             /* stream on, while it is wanted */
    int w, h;
    camera_luma_size(2, &w, &h);                /* 640x360 */
    uint8_t *y = heap_caps_malloc(w * h, MALLOC_CAP_SPIRAM);
    camera_grab_luma(y, 2, NULL, NULL);         /* a frame; ESP_ERR_TIMEOUT after 0.5 s without one */
    camera_stop();
}
```

One task owns the camera between start and stop. While it streams, the
ISP's per-frame complaints (tags `ISP_CCM`, `isp_video`, `ISP`,
`esp_video`) are kept out of the log, and put back on stop.

## What the application must do

**Turn the sensor and the ISP's controller on** in `sdkconfig.defaults`:

```
CONFIG_CAMERA_SC202CS=y
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y
CONFIG_ESP_IPA_AWB_ALGORITHM=y
CONFIG_ESP_IPA_ACC_ALGORITHM=y
```

The controller gives the ISP auto exposure; without it the picture is
near black. Its pipeline for this sensor will not start without auto
white balance and colour correction, so leave those on. Then
`rm sdkconfig` before the next build.

**On ESP32-P4 silicon before v3** (the Tab5s so far are v1.3), supply
esp_ipa yourself. The registry's esp_ipa 2.4.0, which esp_video 2.5.0
asks for, links a library built for v3 whatever the chip, and the first
`camera_init()` dies with an illegal instruction in `create_ipa_array`.
Espressif fixed the selection upstream (esp-video-components 5ec4d12)
but has not released it. Unprofitable Mahjong carries the fixed esp_ipa
in its own `components/esp_ipa`, which wins over the managed one; copy
that, with its VENDORED.md, until a release has it.

**Under warm light**, the SC202CS's default tuning asks the ISP for a
colour matrix it refuses (a blue term past 4.0), every frame. Harmless
-- the last good matrix stays, and the log is kept quiet -- but if the
colours matter, give the sensor a tuning file of your own:

```
CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE=y
CONFIG_CAMERA_SC202CS_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH="camera/sc202cs_ipa.json"
```

Unprofitable Mahjong's `camera/sc202cs_ipa.json` is the default with
its colour matrices made the identity, since it wants only the grey.

**Turn the picture to suit the screen.** The camera is fixed to the
panel; frames come as the sensor sees them. Mahjong's preview turns
them a quarter clockwise in the panel's portrait, and a quarter more
for each quarter the screen turns, and mirrors them, as a front
camera's view usually is.

## Why not in feckless_drivers

esp_video pulls esp_cam_sensor, esp_ipa, esp_h264 and usb_host_uvc.
Everything that uses feckless_drivers and never opens a camera would
build all of it. The only thing the camera needs from the drivers is
the bus, so the dependency runs one way.

## Releases

Push a tag `vX.Y.Z`: the "Sync version from tag" action writes the
version into `idf_component.yml`, commits it, and moves the tag onto
that commit, as in feckless-drivers.

## Licence

MIT; see LICENSE.
