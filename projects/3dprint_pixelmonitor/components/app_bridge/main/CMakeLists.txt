set(PIXEL "ported/features/pixel_screen")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../web/online-update.js" "${CMAKE_CURRENT_LIST_DIR}/../resources/pixel-layout.json" "${CMAKE_CURRENT_LIST_DIR}/../scripts/generate_pixel_layout.py" "${CMAKE_CURRENT_LIST_DIR}/../boards/catalog.json")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed.py" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../web/index.html" "${CMAKE_CURRENT_LIST_DIR}/../web/app.js" "${CMAKE_CURRENT_LIST_DIR}/../web/printer-manager.js" "${CMAKE_CURRENT_LIST_DIR}/../web/style.css" "${CMAKE_CURRENT_LIST_DIR}/../web/pixel-art.js" "${CMAKE_CURRENT_LIST_DIR}/../web/i18n.js" "${CMAKE_CURRENT_LIST_DIR}/../web/forms.js" "${CMAKE_CURRENT_LIST_DIR}/../web/timezone-picker.js" "${CMAKE_CURRENT_LIST_DIR}/../resources/timezones/windows-zones.json" "${CMAKE_CURRENT_LIST_DIR}/../resources/timezones/windows-source.json" "${CMAKE_CURRENT_LIST_DIR}/../resources/locales.json" "${CMAKE_CURRENT_LIST_DIR}/../scripts/generate_locales.py" "${CMAKE_CURRENT_LIST_DIR}/../scripts/embed.py")
idf_component_register(SRCS
 "${MATRIX_BOARD_DIR}/board.cpp"
 "app.cpp" "config.cpp" "network.cpp" "printer.cpp" "telemetry.cpp" "web.cpp" "cloud.cpp" "cloud_sessions.cpp" "lan_validation.cpp" "serial.cpp" "boot_button.cpp" "update.cpp" "firmware_writer.cpp"
 "display/render32/renderer.cpp" "${PIXEL}/core.cpp" "${PIXEL}/connection.cpp" "${PIXEL}/link_protocol.cpp" "${PIXEL}/display_policy.cpp"
 "ported/xtouch/ams_units.c" "ported/platform/idf/ble_runtime.cpp" "ported/xtouch/vault.cpp"
 INCLUDE_DIRS "." "ported" "vendor/ArduinoJson" "../boards/common" "${CMAKE_BINARY_DIR}/boards"
 REQUIRES esp_wifi esp_event esp_netif esp_http_server esp_http_client app_update
 nvs_flash bt mqtt mbedtls esp_timer esp_driver_gpio esp_driver_ledc esp_driver_uart esp_hw_support
 EMBED_FILES "assets/printer-manager_js.gz" "assets/index_html.gz" "assets/app_js.gz" "assets/style_css.gz" "assets/pixel-art_js.gz" "assets/locales_js.gz" "assets/i18n_js.gz" "assets/forms_js.gz" "assets/timezones_js.gz" "assets/timezone-picker_js.gz" "assets/online-update_js.gz")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/ported/platform/idf/json.h>" -Wno-error=use-after-free -Wno-error=misleading-indentation)
