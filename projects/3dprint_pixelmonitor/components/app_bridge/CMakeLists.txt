set(PIXEL "main/ported/features/pixel_screen")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/web/online-update.js" "${CMAKE_CURRENT_LIST_DIR}/resources/pixel-layout.json" "${CMAKE_CURRENT_LIST_DIR}/scripts/generate_pixel_layout.py" "${CMAKE_CURRENT_LIST_DIR}/boards/catalog.json")
execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/scripts/embed.py" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/web/index.html" "${CMAKE_CURRENT_LIST_DIR}/web/app.js" "${CMAKE_CURRENT_LIST_DIR}/web/printer-manager.js" "${CMAKE_CURRENT_LIST_DIR}/web/style.css" "${CMAKE_CURRENT_LIST_DIR}/web/pixel-art.js" "${CMAKE_CURRENT_LIST_DIR}/web/i18n.js" "${CMAKE_CURRENT_LIST_DIR}/web/forms.js" "${CMAKE_CURRENT_LIST_DIR}/web/timezone-picker.js" "${CMAKE_CURRENT_LIST_DIR}/resources/timezones/windows-zones.json" "${CMAKE_CURRENT_LIST_DIR}/resources/timezones/windows-source.json" "${CMAKE_CURRENT_LIST_DIR}/resources/locales.json" "${CMAKE_CURRENT_LIST_DIR}/scripts/generate_locales.py" "${CMAKE_CURRENT_LIST_DIR}/scripts/embed.py")
idf_component_register(SRCS
 "main/update.cpp" "main/firmware_writer.cpp" "ble_controller.c"
 "main/app.cpp" "main/config.cpp" "main/network.cpp" "main/vendor/captive_dns/dns_server.c" "main/printer.cpp" "main/telemetry.cpp" "main/web.cpp" "main/cloud.cpp" "main/cloud_sessions.cpp" "main/lan_validation.cpp" "main/serial.cpp"
 "main/display/render32/renderer.cpp" "${PIXEL}/core.cpp" "${PIXEL}/connection.cpp" "${PIXEL}/link_protocol.cpp" "${PIXEL}/display_policy.cpp"
 "main/ported/xtouch/ams_units.c" "main/ported/platform/idf/ble_runtime.cpp" "main/ported/xtouch/vault.cpp"
 INCLUDE_DIRS "." "main" "main/ported" "main/vendor/ArduinoJson"
 REQUIRES esp_wifi esp_event esp_netif esp_http_server esp_http_client app_update
 nvs_flash bt mqtt mbedtls esp_timer esp_driver_gpio esp_driver_ledc esp_driver_uart esp_hw_support
 EMBED_FILES "main/assets/printer-manager_js.gz" "main/assets/index_html.gz" "main/assets/app_js.gz" "main/assets/style_css.gz" "main/assets/pixel-art_js.gz" "main/assets/locales_js.gz" "main/assets/i18n_js.gz" "main/assets/forms_js.gz" "main/assets/timezones_js.gz" "main/assets/timezone-picker_js.gz" "main/assets/online-update_js.gz")
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include${CMAKE_CURRENT_LIST_DIR}/main/ported/platform/idf/json.h>" -Wno-error=use-after-free -Wno-error=misleading-indentation)

# The pinned S31 SDK's NimBLE headers return integer errors from enum-typed
# inline functions. Restrict its C++ compatibility flag to the BLE callers.
set_source_files_properties(
    main/app.cpp main/web.cpp
    main/ported/platform/idf/ble_runtime.cpp
    ${PIXEL}/connection.cpp
    PROPERTIES COMPILE_OPTIONS "-fpermissive;-Wno-error")
