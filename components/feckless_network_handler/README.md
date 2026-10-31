# Feckless Network Handler for Tab5

Wi-Fi, saved networks and USB Ethernet for the M5Stack Tab5, as an
ESP-IDF component. Pulled out of
[Defeatist Music Player for M5Tab5](https://github.com/Sudrien/defeatist-music-player-for-m5tab5),
where it was written and where it runs; the long comments in the source
are that project's record of why each thing is the way it is, and they
came along unchanged.

| Header | What it is |
|---|---|
| `wifi.h` | The C6 radio over esp_hosted/SDIO2: power through the IO expander, station, background join, scan, the AP for a setup portal, SNTP |
| `wifistore.h` | Up to `WIFISTORE_MAX` saved networks, one NVS blob, ranked against a scan |
| `ethernet.h` | ASIX AX88772 and CDC-ECM (RTL8152/8153) adapters on the USB-A port; `net_online()`, `net_probe()`, which interface is the default route |
| `netlink.h` | When the cable counts as a network, and which interface wins. Header-only and host-tested |
| `ethcfg.h` | Which USB configuration to ask a Realtek adapter for. For whatever owns the USB host |
| `feckless_net.h` | The hooks the application supplies |

## Using it

```yaml
# main/idf_component.yml
dependencies:
  feckless_network_handler:
    git: https://github.com/Sudrien/feckless-network-handler-for-m5tab5.git
    version: "v0.1.0"
```

and `feckless_network_handler` in `main`'s `REQUIRES`.

```c
static const feckless_net_hooks_t hooks = {
    .heap_note          = my_heap_log,          /* all optional */
    .online             = my_network_is_up,
    .ntp_enabled        = my_ntp_wanted,
    .ntp_reply          = my_ntp_plausible,
    .wifi_enabled       = my_wifi_wanted,
    .portal_running     = my_portal_running,
    .portal_stop        = my_portal_stop,
    .usb_register_class = usbhost_register_class, /* needed for Ethernet */
};

nvs_flash_init();
wifistore_init("myapp");            /* your NVS namespace */
feckless_net_set_hooks(&hooks);     /* before wifi_init() and ethernet_init() */
wifi_init(exp2);                    /* the PI4IOE at 0x44 */
ethernet_init();                    /* before your USB host starts */
wifi_apply_settings();
```

`feckless_net.h` gives each hook's default when it is left NULL.

## What the application must do

**Own the USB host.** The Tab5 has one USB-A port and the Ethernet
adapter is one class driver among several on it, so this component does
not install the host stack. It registers its class through
`usb_register_class` and expects the owner to call it before the port is
powered.

**Set esp_hosted's board preset in its own `sdkconfig.defaults`.** A
component cannot set another component's Kconfig. The ones `wifi_init()`
depends on, from the player's `sdkconfig.defaults`:

```
CONFIG_ESP_HOSTED_AUTO_CALL_INIT_BEFORE_APP_MAIN=n   # wifi.c calls esp_hosted_init() itself
CONFIG_ESP32P4_TAB5_C6_BOARD=y
CONFIG_ESP_HOSTED_HOST_RESET_GPIO=15
```

That file explains each of them, and has the SDIO receive, lwIP and
DMA-pool settings the player tuned for streaming; read it before copying
any of those. Then `rm sdkconfig` before the next build: the defaults
only fill keys an existing sdkconfig lacks.

**Do not try to update the C6's firmware.** The M5 slave reports no
version, so esp_hosted logs a major-version mismatch at every start and
works anyway. `idf_component.yml` has the details.

## Tests

```
make -C test
```

The store against a fake NVS, and `netlink.h`'s decisions. Neither needs
ESP-IDF.

## Licence

MIT. See `LICENSE`.
