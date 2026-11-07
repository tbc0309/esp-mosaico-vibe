#include "bridge_service.h"
#include "print_sound.h"
#include "bsp/esp_mosaico.h"
#include "esp_iris.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "tusb.h"
extern "C" {
#include "iris_ota_support.h"
}
#include "nvs_flash.h"
#include "lvgl.h"
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr int side = 32;
constexpr int scale = 13;
constexpr int canvasSide = side * scale;
constexpr int footerHeight = 32;
constexpr int footerBottom = 14;
constexpr int footerInset = 28;
constexpr int footerRadius = 13;
// The onboard flow follows Vibe Mode: scan, select, enter a password, connect.
struct WifiPanel {
    lv_obj_t *entry{}, *page{}, *list{}, *ssid{}, *password{}, *keyboard{}, *status{};
    std::string options, error;
    bool connecting = false;
    bool online = false;

    static WifiPanel &owner(lv_event_t *event) {
        return *static_cast<WifiPanel *>(lv_event_get_user_data(event));
    }
    lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, lv_event_cb_t callback) {
        auto *btn = lv_button_create(parent);
        lv_obj_set_pos(btn,x,y);
        auto *label = lv_label_create(btn);
        lv_label_set_text(label,text);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn,callback,LV_EVENT_CLICKED,this);
        return btn;
    }
    lv_obj_t *field(const char *hint, int y, unsigned limit, bool secret) {
        auto *input = lv_textarea_create(page);
        lv_textarea_set_one_line(input,true);
        lv_textarea_set_max_length(input,limit);
        lv_textarea_set_password_mode(input,secret);
        lv_textarea_set_placeholder_text(input,hint);
        lv_obj_set_pos(input,16,y);
        lv_obj_set_width(input,440);
        lv_obj_add_event_cb(input,[](lv_event_t *event) {
            auto &self = owner(event);
            lv_keyboard_set_textarea(self.keyboard,static_cast<lv_obj_t *>(lv_event_get_target(event)));
            lv_obj_remove_flag(self.keyboard,LV_OBJ_FLAG_HIDDEN);
        },LV_EVENT_CLICKED,this);
        return input;
    }
    void start(lv_obj_t *screen) {
        entry = button(screen,"Wi-Fi",0,0,[](lv_event_t *event) {
            auto &self = owner(event);
            if (!self.page) self.createPage(lv_screen_active());
            lv_obj_remove_flag(self.page,LV_OBJ_FLAG_HIDDEN);
            bridgeWifiScan();
        });
        lv_obj_set_size(entry, 78, footerHeight);
        lv_obj_set_style_radius(entry, footerRadius, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(entry, 0, LV_PART_MAIN);
        lv_obj_align(entry, LV_ALIGN_BOTTOM_RIGHT, -footerInset, -footerBottom);
    }
    void createPage(lv_obj_t *screen) {
        page = lv_obj_create(screen);
        lv_obj_set_size(page,480,480);
        lv_obj_set_style_pad_all(page,0,0);
        lv_obj_remove_flag(page,LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(page);
        auto *title = lv_label_create(page);
        lv_label_set_text(title,"Wi-Fi setup");
        lv_obj_set_pos(title,16,18);
        button(page,"Back",382,8,[](lv_event_t *event) {
            auto &self = owner(event);
            lv_textarea_set_text(self.password,"");
            lv_obj_add_flag(self.keyboard,LV_OBJ_FLAG_HIDDEN);
            self.connecting = false;
            lv_obj_add_flag(self.page,LV_OBJ_FLAG_HIDDEN);
        });
        button(page,"Scan",382,56,[](lv_event_t *event) {
            owner(event).error.clear();
            bridgeWifiScan();
        });
        list = lv_dropdown_create(page);
        lv_obj_set_pos(list,16,56);
        lv_obj_set_width(list,340);
        lv_obj_add_event_cb(list,[](lv_event_t *event) {
            auto &self = owner(event);
            if (!lv_dropdown_get_selected(self.list)) return;
            char name[33]{};
            lv_dropdown_get_selected_str(self.list,name,sizeof(name));
            lv_textarea_set_text(self.ssid,name);
            lv_textarea_set_text(self.password,"");
        },LV_EVENT_VALUE_CHANGED,this);
        ssid = field("Network name (SSID)",108,32,false);
        password = field("Wi-Fi password",156,63,true);
        status = lv_label_create(page);
        lv_obj_set_width(status,330);
        lv_obj_set_pos(status,16,213);
        button(page,"Connect",362,205,[](lv_event_t *event) {
            auto &self = owner(event);
            self.error = bridgeWifiConnect(lv_textarea_get_text(self.ssid),
                                           lv_textarea_get_text(self.password));
            self.connecting = self.error.empty();
            if (self.connecting) {
                lv_textarea_set_text(self.password,"");
                lv_obj_add_flag(self.keyboard,LV_OBJ_FLAG_HIDDEN);
            }
        });
        keyboard = lv_keyboard_create(page);
        lv_obj_set_size(keyboard,476,200);
        lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);
        lv_obj_add_event_cb(keyboard,[](lv_event_t *event) {
            if (lv_event_get_code(event)==LV_EVENT_READY || lv_event_get_code(event)==LV_EVENT_CANCEL)
                lv_obj_add_flag(owner(event).keyboard,LV_OBJ_FLAG_HIDDEN);
        },LV_EVENT_ALL,this);
        lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN);
    }
    void refresh(const std::string &address) {
        const bool connected = bridgeWifiOnline();
        if (connected != online) {
            online = connected;
            if (online) lv_obj_set_style_bg_color(entry, lv_color_hex(0x535962), LV_PART_MAIN);
            else lv_obj_remove_local_style_prop(entry, LV_STYLE_BG_COLOR, LV_PART_MAIN);
        }
        if (!page || lv_obj_has_flag(page,LV_OBJ_FLAG_HIDDEN)) return;
        const auto wifi = bridgeWifiState();
        if (connecting && wifi.online) {
            connecting = false;
            lv_obj_add_flag(keyboard,LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(page,LV_OBJ_FLAG_HIDDEN);
            return;
        }
        const std::string choices = "Select a network" +
            (wifi.networks.empty() ? std::string() : "\n"+wifi.networks);
        if (options != choices) {
            options = choices;
            lv_dropdown_set_options(list,options.c_str());
        }
        std::string message = "Select a network or enter its name.";
        if (error=="storage_error") message = "Could not save Wi-Fi settings.";
        else if (!error.empty()) message = "Check SSID and password length.";
        else if (wifi.scanning) message = "Scanning for Wi-Fi...";
        else if (wifi.online) message = "Connected: "+address;
        else if (wifi.error=="wifi_auth_failed") message = "Check the Wi-Fi password.";
        else if (wifi.error=="wifi_not_found") message = "Wi-Fi network not found.";
        else if (wifi.error=="scan_failed" || wifi.error=="scan_timeout") message = "Scan failed. Retry.";
        else if (wifi.phase=="connecting" || wifi.phase=="retrying") message = "Connecting to Wi-Fi...";
        if (message != lv_label_get_text(status)) lv_label_set_text(status,message.c_str());
    }
};

class BatteryBadge {
public:
    void start(lv_obj_t *screen) {
        label = lv_label_create(screen);
        lv_obj_set_size(label, 132, footerHeight);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        const auto lineHeight = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
        lv_obj_set_style_pad_top(label, std::max(0, (footerHeight - int(lineHeight)) / 2), 0);
        lv_obj_set_style_radius(label, footerRadius, 0);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
        lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -footerBottom);
        show(-1, false, false);
    }

    void poll() {
        const auto now = esp_timer_get_time();
        if (now < nextRead) return;
        nextRead = now + 5000000;
        uint16_t voltage = 0, flags = 0, current = 0, soc = 0;
        const bool valid = attach() && read(0x08, voltage) && read(0x0a, flags) &&
                           read(0x0c, current) && read(0x2c, soc) &&
                           voltage > 0 && soc <= 100 && (flags & batteryPresent);
        const bool usb = tud_mounted() && !tud_suspended();
        const bool full = valid && usb && !(flags & discharging) && (flags & fullCharged);
        const bool charging = valid && !full && !(flags & discharging) && int16_t(current) > 0;
        const int percent = valid ? soc : -1;
        if (percent == lastPercent && charging == lastCharging && full == lastFull) return;
        if (!bsp_display_lock(100)) return;
        show(percent, charging, full);
        bsp_display_unlock();
    }

private:
    static constexpr unsigned discharging = 1u << 0;   // BQ27220 BatteryStatus.DSG
    static constexpr unsigned batteryPresent = 1u << 3;
    static constexpr unsigned fullCharged = 1u << 9;
    lv_obj_t *label = nullptr;
    i2c_master_dev_handle_t gauge = nullptr;
    int64_t nextRead = 0;
    int lastPercent = -1;
    bool lastCharging = false, lastFull = false;

    bool attach() {
        if (gauge) return true;
        if (bsp_battery_init() != ESP_OK) return false;
        auto bus = bsp_i2c_get_handle();
        if (!bus) return false;
        i2c_device_config_t config{};
        config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        config.device_address = BSP_BATTERY_I2C_ADDR;
        config.scl_speed_hz = BSP_BATTERY_I2C_SPEED_HZ;
        return i2c_master_bus_add_device(bus, &config, &gauge) == ESP_OK;
    }

    bool read(uint8_t command, uint16_t &value) {
        uint8_t bytes[2]{};
        if (i2c_master_transmit_receive(gauge, &command, 1, bytes, sizeof(bytes), 100) != ESP_OK)
            return false;
        value = uint16_t(bytes[0]) | uint16_t(bytes[1]) << 8;
        return true;
    }

    void show(int percent, bool charging, bool full) {
        lastPercent = percent;
        lastCharging = charging;
        lastFull = full;
        uint32_t background = 0x252930, foreground = percent < 0 ? 0xaeb4bf : 0xe3e7ec;
        if (charging || full) { background = 0x183626; foreground = 0x8af0ad; }
        else if (percent >= 0 && percent <= 5) { background = 0x432323; foreground = 0xff9f9f; }
        else if (percent >= 0 && percent <= 20) { background = 0x372b17; foreground = 0xffd383; }
        lv_obj_set_style_bg_color(label, lv_color_hex(background), 0);
        lv_obj_set_style_text_color(label, lv_color_hex(foreground), 0);
        const char *icon = percent >= 95 ? LV_SYMBOL_BATTERY_FULL :
                           percent >= 70 ? LV_SYMBOL_BATTERY_3 :
                           percent >= 40 ? LV_SYMBOL_BATTERY_2 :
                           percent >= 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        if (percent < 0) lv_label_set_text(label, LV_SYMBOL_BATTERY_EMPTY "  --%");
        else lv_label_set_text_fmt(label, "%s  %d%%%s", icon, percent,
                                   full ? "  " LV_SYMBOL_OK : charging ? "  " LV_SYMBOL_CHARGE : "");
    }
};

struct Display {
    BatteryBadge battery;
    WifiPanel wifi;
    lv_obj_t *canvas = nullptr;
    lv_obj_t *addressLabel = nullptr;
    uint16_t *buffer = nullptr;
    uint8_t pixels[pixel_screen::pixels]{};

    bool start() {
        if (!bsp_display_start()) return false;
        buffer = static_cast<uint16_t *>(heap_caps_malloc(
            canvasSide * canvasSide * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!buffer || !bsp_display_lock(1000)) return false;
        auto *screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
        canvas = lv_canvas_create(screen);
        lv_canvas_set_buffer(canvas, buffer, canvasSide, canvasSide, LV_COLOR_FORMAT_RGB565);
        lv_canvas_fill_bg(canvas, lv_color_black(), LV_OPA_COVER);
        lv_obj_align(canvas,LV_ALIGN_TOP_MID,0,8);
        addressLabel = lv_label_create(screen);
        lv_label_set_text(addressLabel, "IP --");
        lv_obj_set_style_text_color(addressLabel, lv_color_white(), 0);
        const auto addressHeight = lv_font_get_line_height(lv_obj_get_style_text_font(addressLabel, LV_PART_MAIN));
        lv_obj_align(addressLabel, LV_ALIGN_BOTTOM_LEFT, footerInset,
                     -footerBottom - (footerHeight - int(addressHeight)) / 2);
        battery.start(screen);
        wifi.start(screen);
        bsp_display_unlock();
        return true;
    }

    bool present(const pixel_screen::Snapshot &snapshot, unsigned phase) {
        pixel_screen::render(snapshot, phase, pixels);
        const auto address = bridgeAddress();
        if (!bsp_display_lock(100)) return false;
        bool changed = false;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const auto rgb = pixel_screen::rgb(pixels[y * side + x],
                                                  snapshot.filamentColor, snapshot.feedColor);
                const uint16_t color = ((rgb >> 8) & 0xf800) |
                                       ((rgb >> 5) & 0x07e0) | ((rgb >> 3) & 0x1f);
                auto *cell = buffer + y * scale * canvasSide + x * scale;
                if (*cell == color) continue;
                changed = true;
                for (int dy = 0; dy < scale; ++dy)
                    std::fill_n(cell + dy * canvasSide, scale, color);
            }
        }
        const char *addressText = !bridgeWifiOnline() || address.empty() ? "IP --" : address.c_str();
        if (strcmp(addressText, lv_label_get_text(addressLabel))) lv_label_set_text(addressLabel, addressText);
        if (changed) lv_obj_invalidate(canvas);
        wifi.refresh(address);
        bsp_display_unlock();
        return true;
    }
};
class Standby {
public:
    bool asleep() const { return sleeping; }

    void poll(const pixel_screen::Snapshot &picture, int64_t now, int idleMinutes) {
        const int64_t idleLimit = idleMinutes * 60 * 1000000LL;
        using pixel_screen::State;
        const bool printActive = picture.state == State::Preparing ||
                                 picture.state == State::Printing;
        if (sleeping && printActive && !wasPrinting) wake("print started", now);
        wasPrinting = printActive;
        if (printActive) idleSince = now;
        if (!sleeping && !printActive && now - idleSince >= idleLimit)
            sleep(idleSince);
        if (sleeping && now - idleSince >= idleLimit && !pixelSuspended) {
            bridgeSuspendPixel(true);
            pixelSuspended = true;
            ESP_LOGI("standby", "External pixel link paused after configured idle timeout");
        }

        const bool level = gpio_get_level(BSP_BUTTON_AI_GPIO) == BSP_BUTTON_ACTIVE_LEVEL;
        if (level != rawPressed) {
            rawPressed = level;
            changedAt = now;
        }
        if (level != pressed && now - changedAt >= debounce) {
            pressed = level;
            if (pressed) {
                pressedAt = now;
                handled = sleeping;
                if (sleeping) wake("AI button", now);
            } else if (!handled) {
                if (now - pressedAt >= longPress) sleep(now);
                else bridgeToggleLayout();
            }
        }
        if (pressed && !handled && now - pressedAt >= longPress) {
            handled = true;
            sleep(now);
        }
    }

private:
    static constexpr int64_t debounce = 30000;
    static constexpr int64_t longPress = 2000000;
    bool sleeping = false, pixelSuspended = false, wasPrinting = false;
    bool rawPressed = false, pressed = false, handled = false;
    int64_t changedAt = 0, pressedAt = 0, idleSince = 0;
    wifi_ps_type_t priorPowerSave = WIFI_PS_MIN_MODEM;

    void sleep(int64_t now) {
        if (bsp_display_off() != ESP_OK) return;
        sleeping = true;
        idleSince = now;
        if (esp_wifi_get_ps(&priorPowerSave) == ESP_OK)
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
        ESP_LOGI("standby", "Display off; MQTT remains available for print wake");
    }

    void wake(const char *reason, int64_t now) {
        if (bsp_display_on() != ESP_OK) return;
        sleeping = false;
        idleSince = now;
        pixelSuspended = false;
        bridgeSuspendPixel(false);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(priorPowerSave));
        ESP_LOGI("standby", "Awake: %s", reason);
    }
};

Display display;
}

extern "C" void app_main() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    iris_ota_support_start();
    if (!display.start() || !bridgeStart()) return;
    ESP_ERROR_CHECK(gpio_set_direction(BSP_BUTTON_AI_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BSP_BUTTON_AI_GPIO, GPIO_PULLUP_ONLY));
    Standby standby;
    unsigned lastFrame = UINT32_MAX;
    bool healthy = false;
    int appliedBrightness = 0;
    while (true) {
        const auto picture = bridgePoll();
        const auto now = esp_timer_get_time();
        const auto settings = bridgeDisplaySettings();
        standby.poll(picture, now, settings.idleMinutes);
        printSoundPoll(picture.state);
        if (standby.asleep()) appliedBrightness = 0;
        else if (appliedBrightness != settings.brightness &&
                 bsp_display_brightness_set(settings.brightness) == ESP_OK)
            appliedBrightness = settings.brightness;
        if (!standby.asleep()) display.battery.poll();
        const unsigned frame = now / 100000;
        if (!standby.asleep() && frame != lastFrame && display.present(picture, frame)) {
            lastFrame = frame;
            if (!healthy) healthy = esp_iris_mark_healthy() == ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(standby.asleep() ? 100 : 50));
    }
}
