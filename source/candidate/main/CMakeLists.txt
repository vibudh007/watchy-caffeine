idf_component_register(SRCS "main.c" "board.c" "panel.c" "portal.c" "portal_dns.c"
    EMBED_FILES "bma423.config"
    PRIV_REQUIRES watch bootloader_support esp_driver_gpio esp_driver_i2c esp_driver_spi esp_adc esp_timer
                  esp_wifi esp_netif esp_event esp_http_server nvs_flash esp_driver_uart lwip)
if(WATCHY_BENCH_DEMO)
    target_sources(${COMPONENT_LIB} PRIVATE "demo.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WATCHY_BENCH_DEMO=1)
endif()
if(WATCHY_BENCH_UI)
    target_sources(${COMPONENT_LIB} PRIVATE "ui_demo.c")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WATCHY_BENCH_UI=1)
endif()
if(DEFINED WATCHY_INITIAL_UTC_OFFSET)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WATCHY_INITIAL_UTC_OFFSET=${WATCHY_INITIAL_UTC_OFFSET})
endif()
if(WATCHY_NIGHT_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WATCHY_NIGHT_TEST=1)
endif()
