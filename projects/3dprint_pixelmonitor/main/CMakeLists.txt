idf_component_register(SRCS "main.cpp" "print_sound.cpp" REQUIRES app_bridge esp-mosaico-bsp esp_iris esp_mosaico_app_recovery nvs_flash PRIV_REQUIRES esp_driver_i2c tinyusb esp_tinyusb)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
