# feckless-libs-for-m5tab5: feckless_network_handler
#
# The esp_wifi_remote / esp_hosted / ASIX / CDC-ECM components come from
# idf_component.yml. esp_wifi, esp_netif and esp_event are PUBLIC because
# the headers here are how an application reaches them: wifi.h hands out
# an esp_netif, and callers compare against ESP_ERR_WIFI_* codes.
#
# SPDX-License-Identifier: MIT

idf_component_register(
    SRCS "src/feckless_net.c" "src/wifistore.c" "src/wifi.c" "src/ethernet.c"
         "src/hostedwrap.c"
         "src/feckless_cert.c" "src/feckless_https.c" "src/feckless_redirect.c"   # 8004
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c esp_wifi esp_netif esp_event esp_wifi_remote
             esp_hosted
             esp_http_server   # 8004: feckless_https.h's httpd_config_t
    PRIV_REQUIRES esp_timer lwip nvs_flash usb mbedtls   # mbedtls: 6003, wifi_psk(); 8004, feckless_cert
             esp_https_server mdns   # 8004: feckless_https.c; mdns: the name the redirect sends to
             esp_usbh_asix iot_eth iot_usbh_ecm iot_usbh_cdc
)

# 5092: esp_hosted's per-packet buffers from PSRAM. See src/hostedwrap.c.
# --wrap turns esp_hosted's calls to eh_host_port_dma_alloc() into calls
# to __wrap_eh_host_port_dma_alloc(); -u makes the linker take that
# definition from this component's archive whatever the link order.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=eh_host_port_dma_alloc"
    "-Wl,-u,__wrap_eh_host_port_dma_alloc")
