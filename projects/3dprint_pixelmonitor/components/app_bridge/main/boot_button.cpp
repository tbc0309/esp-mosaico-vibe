#include "app.h"
#include "boot_button_policy.h"
#include "board.h"
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace bridge {
namespace {
std::atomic<bool> feedback{false};
void worker(void *) {
    BootButtonPolicy button;
    bool pressed = false;
    for (;;) {
        uint32_t tick = uint32_t(esp_timer_get_time() / 1000);
        bool level = board::bootPressed();
        if (level != pressed) { pressed = level; ESP_LOGI("boot-button", "BOOT %s", level ? "pressed" : "released"); }
        BootAction action = button.update(level, tick);
        uint32_t held = button.heldMs(tick);
        feedback = held >= 3000;
        if (feedback) board::statusLed(!((tick / (held >= 10000 ? 80 : 250)) % 2));
        if (action != BootAction::None) {
            bool all = action == BootAction::FactoryReset;
            ESP_LOGW("boot-button", "%s reset requested", all ? "Factory" : "Wi-Fi");
            if (configReset(all)) { ESP_LOGW("boot-button", "Reset saved; restarting"); esp_restart(); }
            ESP_LOGE("boot-button", "Reset failed; existing settings retained where possible");
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
}
bool bootButtonFeedback() { return feedback; }
void bootButtonBegin() {
    if (xTaskCreate(worker, "boot-button", 4096, nullptr, 2, nullptr) != pdPASS)
        ESP_LOGE("boot-button", "Could not start BOOT button worker");
}
}
