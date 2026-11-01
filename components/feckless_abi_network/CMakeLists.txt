# feckless-libs-for-m5tab5: feckless_abi_network
#
# The feckless ABI's network section (6004). A launcher that links this
# gives its apps feckless_network_handler, lwIP's sockets, esp_netif,
# the HTTP server with WebSockets, and the HTTP client with the
# certificate bundle: the table esp_elf resolves them against, generated
# from feckless_abi's sections/network.txt, and feckless_abi_network_v1,
# which every such app refers to. An app names this component too, for
# the headers; its ELF links none of it.
#
# WHOLE_ARCHIVE: nothing in a launcher refers to the table but
# feckless_abi_run()'s weak reference, which pulls nothing out of an
# archive by itself.
#
# SPDX-License-Identifier: MIT

set(_table "${CMAKE_CURRENT_BINARY_DIR}/feckless_abi_network_syms.c")

idf_component_register(
    SRCS "src/feckless_abi_network.c" "${_table}"
    INCLUDE_DIRS "include"
    REQUIRES feckless_abi feckless_network_handler lwip esp_netif esp_wifi
             esp_http_server esp_http_client mbedtls
    PRIV_REQUIRES elf_loader
    WHOLE_ARCHIVE
)

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(_abi_dir feckless_abi COMPONENT_DIR)

    # One tag, as feckless_abi checks for its own siblings.
    set(_versions "")
    foreach(_dir "${CMAKE_CURRENT_LIST_DIR}" "${_abi_dir}")
        file(STRINGS "${_dir}/idf_component.yml" _v REGEX "^version:")
        string(REGEX REPLACE "^version:[ \t]*\"?([^\" \t]*)\"?.*" "\\1" _v "${_v}")
        list(APPEND _versions "${_v}")
    endforeach()
    list(GET _versions 0 _own)
    list(GET _versions 1 _abiv)
    if(NOT _own STREQUAL _abiv)
        message(FATAL_ERROR "feckless_abi_network is ${_own}, feckless_abi is ${_abiv}: "
                "name every feckless component at the same tag")
    endif()

    # The configuration both sides must share: each is a struct the app
    # lays out and the launcher's code reads, or a function that exists
    # only with it. The values are ESP-IDF 6.1's defaults on the P4 but
    # WebSockets, which Defeatist's remote needs.
    set(_bad "")
    if(NOT CONFIG_HTTPD_MAX_URI_LEN EQUAL 512)
        list(APPEND _bad "CONFIG_HTTPD_MAX_URI_LEN=512 (httpd_req_t's uri, is ${CONFIG_HTTPD_MAX_URI_LEN})")
    endif()
    if(NOT CONFIG_HTTPD_WS_SUPPORT)
        list(APPEND _bad "CONFIG_HTTPD_WS_SUPPORT=y (httpd_uri_t's is_websocket, the httpd_ws_ calls)")
    endif()
    if(CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT OR CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT)
        list(APPEND _bad "CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT=n and _POST_=n (httpd_uri_t)")
    endif()
    if(NOT CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS)
        list(APPEND _bad "CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y (esp_http_client_config_t)")
    endif()
    if(CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT)
        list(APPEND _bad "CONFIG_ESP_HTTP_CLIENT_ENABLE_CUSTOM_TRANSPORT=n (esp_http_client_config_t)")
    endif()
    if(NOT CONFIG_ESP_TLS_USE_DS_PERIPHERAL)
        list(APPEND _bad "CONFIG_ESP_TLS_USE_DS_PERIPHERAL=y (esp_http_client_config_t)")
    endif()
    if(CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS)
        list(APPEND _bad "CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS=n (esp_http_client_config_t)")
    endif()
    if(CONFIG_MBEDTLS_DYNAMIC_BUFFER)
        list(APPEND _bad "CONFIG_MBEDTLS_DYNAMIC_BUFFER=n (esp_http_client_config_t)")
    endif()
    if(CONFIG_MBEDTLS_HARDWARE_ECDSA_SIGN)
        list(APPEND _bad "CONFIG_MBEDTLS_HARDWARE_ECDSA_SIGN=n (esp_http_client_config_t)")
    endif()
    if(NOT CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
        list(APPEND _bad "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y (esp_crt_bundle_attach)")
    endif()
    if(NOT CONFIG_LWIP_IPV4 OR NOT CONFIG_LWIP_IPV6)
        list(APPEND _bad "CONFIG_LWIP_IPV4=y and CONFIG_LWIP_IPV6=y (ip_addr_t, sockaddr_storage)")
    endif()
    if(_bad)
        string(REPLACE ";" "\n  " _bad "${_bad}")
        message(FATAL_ERROR "feckless_abi_network: the network section needs\n  ${_bad}")
    endif()
    # 8004: feckless_https_start() is httpd_ssl_start() behind the device
    # certificate, which esp_https_server only builds with this. A launcher
    # without it fails to link; an app's own build does not need it, so it
    # is not an error here.
    if(NOT CONFIG_ESP_HTTPS_SERVER_ENABLE)
        message(WARNING "feckless_abi_network: CONFIG_ESP_HTTPS_SERVER_ENABLE=y is needed "
                "by a launcher: the section's feckless_https_start() calls httpd_ssl_start()")
    endif()
    # Not fatal: the launcher's sockets are every app's, and Defeatist
    # wants 28 (its sdkconfig.defaults says why).
    if(CONFIG_LWIP_MAX_SOCKETS LESS 16)
        message(WARNING "feckless_abi_network: CONFIG_LWIP_MAX_SOCKETS=${CONFIG_LWIP_MAX_SOCKETS}; "
                "an app's server and client share them, and 16 or more is recommended")
    endif()

    include("${_abi_dir}/cmake/abitable.cmake")
    feckless_abi_write_table("${_table}" "${_abi_dir}/sections/network.txt"
                             g_feckless_abi_network_elfsyms
                             "\"feckless_abi_network_v1\"" feckless_abi_network_v1)
endif()
