# feckless_abi_network

The feckless ABI's network section: `feckless_network_handler`, lwIP's
sockets, esp_netif, ESP-IDF's HTTP server with WebSockets and its HTTP
client with the certificate bundle, for an app's ELF, from a launcher
that links this component. The list is feckless_abi's
`sections/network.txt`; feckless_abi's README says what a section is.

## A launcher

Name `feckless_abi_network` and `feckless_network_handler` in the
manifest, at the tag of the rest, and `feckless_abi_network` in main's
`REQUIRES`. The network runs under the launcher's build, so it does what
feckless_network_handler's README asks of an application -- its
esp_hosted and Wi-Fi settings -- and holds to the configuration below.
It need not bring the network up: `wifi_init()` and `ethernet_init()`
are the app's. NVS must be up, for the saved networks.

## An app

```yaml
  feckless_abi_network:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_abi_network
    version: "v0.6.0"
  feckless_network_handler:
    git: https://github.com/Sudrien/feckless-libs-for-m5tab5.git
    path: components/feckless_network_handler
    version: "v0.6.0"
```

beside the rest, and `feckless_abi_network` in main's `REQUIRES`. Then
`feckless_abi_require_network()` first thing in `app_main()`, beside
`feckless_abi_require()`. Sockets are lwIP's: `socket()` and the rest
compile to `lwip_socket()` and the rest, which the section exports;
`close()`, `read()` and `write()` on a socket go through IDF's VFS and are
the storage section's. Mbed TLS is not exported: the WPA2 PSK is the
handler's `wifi_psk()`, and HTTPS is the HTTP client's, with
`esp_crt_bundle_attach`.

## The configuration both sides share

An app lays these structs out by its own build, and the launcher's code
reads them, so both builds are held to one configuration, and this
component's CMakeLists.txt fails either that differs:

| Option | Why |
|---|---|
| `CONFIG_HTTPD_MAX_URI_LEN=512` | `httpd_req_t`'s `uri` |
| `CONFIG_HTTPD_WS_SUPPORT=y`, the handshake callbacks `=n` | `httpd_uri_t`, the `httpd_ws_` calls |
| `CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y`, `_CUSTOM_TRANSPORT=n` | `esp_http_client_config_t` |
| `CONFIG_ESP_TLS_USE_DS_PERIPHERAL=y`, `_CLIENT_SESSION_TICKETS=n` | `esp_http_client_config_t` |
| `CONFIG_MBEDTLS_DYNAMIC_BUFFER=n`, `_HARDWARE_ECDSA_SIGN=n` | `esp_http_client_config_t` |
| `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` | `esp_crt_bundle_attach` |
| `CONFIG_LWIP_IPV4=y`, `CONFIG_LWIP_IPV6=y` | `ip_addr_t`, `sockaddr_storage` |

All are ESP-IDF 6.1's defaults on the P4 but WebSockets. It warns below
16 `CONFIG_LWIP_MAX_SOCKETS`: the launcher's sockets are every app's.
Changing one is a new section version, `network_v2`.

## Licence

MIT. See `LICENSE` at the repository's top.
