#include "esp_bt.h"
#include "esp_err.h"

esp_err_t mosaico_bt_stop(void)
{
    esp_err_t err = ESP_OK;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        err = esp_bt_controller_disable();
    }
    if (err == ESP_OK && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        err = esp_bt_controller_deinit();
    }
    return err;
}

int mosaico_bt_state(void)
{
    return esp_bt_controller_get_status();
}
