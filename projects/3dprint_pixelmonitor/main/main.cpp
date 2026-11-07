#include "bridge_service.h"
#include "print_sound.h"
#include "wake_input.h"
#include "wake_input_checks.h"
#include "bsp/esp_mosaico.h"
#include "esp_iris.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_lv_adapter_input.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
extern "C" {
#include "iris_ota_support.h"
}
#include "nvs_flash.h"
#include "lvgl.h"
#include "tusb.h"
#include <algorithm>
#include <atomic>
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

    void poll(bool standby = false, uint32_t sleepCount = 0, uint32_t sleepMicros = 0, bool usbSleepLock = false) {
        const auto now = esp_timer_get_time();
        if (now < nextRead) return;
        nextRead = now + (standby ? 30000000 : 5000000);
        uint16_t voltage = 0, flags = 0, current = 0, soc = 0;
        const bool valid = attach() && read(0x08, voltage) && read(0x0a, flags) &&
                           read(0x0c, current) && read(0x2c, soc) &&
                           voltage > 0 && soc <= 100 && (flags & batteryPresent);
        // The gauge can enter relaxation after charge termination, including on a wall charger.
        const int gauging = -1;
        const bool full = valid && (flags & fullCharged) && int16_t(current) >= 0;
        const bool charging = valid && !full && !(flags & discharging) && int16_t(current) > 0;
        const int percent = full ? 100 : valid ? soc : -1;
        uint16_t average = 0, remaining = 0, capacity = 0, coulomb = 0, timeToFull = 0;
        if (valid && read(0x14, average) && read(0x10, remaining) && read(0x12, capacity) &&
            read(0x22, coulomb) && read(0x18, timeToFull)) {
            ESP_LOGI("battery-cycle", "soc=%u mv=%u ma=%d avg=%d rm=%u fcc=%u cc=%d ttf=%u flags=%04x fc=%d tca=%d",
                     unsigned(soc), unsigned(voltage), int16_t(current), int16_t(average),
                     unsigned(remaining), unsigned(capacity), int16_t(coulomb), unsigned(timeToFull),
                     unsigned(flags), bool(flags & fullCharged), bool(flags & (1u << 6)));
            BridgeBatterySample sample;
            sample.uptimeMs=esp_timer_get_time()/1000;sample.soc=soc;sample.voltageMv=voltage;
            sample.displaySleeping=standby;sample.lightSleepCount=sleepCount;sample.lightSleepUs=sleepMicros;
            sample.usbMounted=tud_mounted();sample.usbSuspended=tud_suspended();sample.usbSleepLock=usbSleepLock;
            sample.currentMa=int16_t(current);sample.averageMa=int16_t(average);sample.remainingMah=remaining;
            sample.fullCapacityMah=capacity;sample.flags=flags;sample.timeToFull=timeToFull;sample.gauging=gauging;
            bridgePublishBatterySample(sample);
        }

        if (percent == lastPercent && charging == lastCharging && full == lastFull) return;
        if (!bsp_display_lock(100)) return;
        show(percent, charging, full);
        bsp_display_unlock();
        if (valid && (full || soc >= 90)) {
            ESP_LOGI("battery", "soc=%u displayed=%d current=%d mV=%u flags=0x%04x full=%d",
                     unsigned(soc), percent, int16_t(current), unsigned(voltage), unsigned(flags), full);
        }
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
        if (i2c_master_bus_add_device(bus, &config, &gauge) != ESP_OK) return false;
        return true;
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
        bsp_display_config_t config = BSP_DISPLAY_DEFAULT_CONFIG();
        // PSRAM frames are staged through internal DMA RAM. Bound its peak
        // independently of frame size, leaving room for Wi-Fi/BLE and PM.
        config.spi_queue_depth = 2;
        if (!bsp_display_start_with_config(&config)) return false;
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

    bool present(const pixel_screen::Snapshot &snapshot, unsigned phase, bool force = false) {
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
        if (changed || force) lv_obj_invalidate(canvas);
        wifi.refresh(address);
        if (force) {
            lv_obj_invalidate(lv_screen_active());
            // The QSPI flush completion notifies the LVGL worker. Refresh must
            // run there: lv_refr_now() here would block the application task.
            lv_timer_ready(lv_display_get_refr_timer(bsp_display_get()));
            ESP_LOGI("standby","Wake frame queued for LVGL worker");
        }
        bsp_display_unlock();
        return true;
    }
};
class Standby {
public:
    bool asleep() const { return sleeping; }
    bool usbLocked() const { return usbHeld; }
    uint32_t sleepCount() const { return lightSleepCount.load(std::memory_order_relaxed); }
    uint32_t sleepMicros() const { return lightSleepUs.load(std::memory_order_relaxed); }
    bool redrawRequested() const { return redrawPending; }
    void redrawn() { redrawPending=false; }
    bool start() {
        if (gpio_set_direction(BSP_BUTTON_AI_GPIO,GPIO_MODE_INPUT)!=ESP_OK ||
            gpio_set_pull_mode(BSP_BUTTON_AI_GPIO,GPIO_PULLUP_ONLY)!=ESP_OK) return false;
        mainTask=xTaskGetCurrentTaskHandle();
        // Driver-owned locks protect radio/SPI transfers during DFS and sleep.
        esp_pm_config_t pm{CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,40,true};
        if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"screen",&cpuLock)!=ESP_OK ||
            esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"screen",&awakeLock)!=ESP_OK)
            return false;
        if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP,0,"usb-host",&usbLock)!=ESP_OK) return false;
        if (esp_pm_lock_acquire(cpuLock)!=ESP_OK || esp_pm_lock_acquire(awakeLock)!=ESP_OK)
            return false;
        if (xTaskCreate(inputWorker,"wake-input",3072,this,4,&inputTask)!=pdPASS) return false;
        auto ret=gpio_install_isr_service(0);
        if (ret!=ESP_OK && ret!=ESP_ERR_INVALID_STATE) return false;
        if (gpio_set_intr_type(BSP_BUTTON_AI_GPIO,GPIO_INTR_LOW_LEVEL)!=ESP_OK ||
            gpio_isr_handler_add(BSP_BUTTON_AI_GPIO,buttonInterrupt,this)!=ESP_OK) return false;
        gpio_intr_disable(BSP_LCD_TOUCH_INT);
        esp_lv_adapter_touch_callbacks_t callbacks{};
        callbacks.on_interrupt=touchInterrupt;callbacks.user_ctx=this;
        if (!bsp_display_lock(200)) return false;
        ret=esp_lv_adapter_set_touch_callbacks(bsp_display_get_input_dev(),&callbacks);
        bsp_display_unlock();
        if (ret!=ESP_OK) return false;
        // These are wake sources for automatic Light-sleep, not Deep-sleep.
        if (gpio_wakeup_enable(BSP_BUTTON_AI_GPIO,GPIO_INTR_LOW_LEVEL)!=ESP_OK ||
            gpio_wakeup_enable(BSP_LCD_TOUCH_INT,GPIO_INTR_LOW_LEVEL)!=ESP_OK ||
            esp_sleep_enable_gpio_wakeup()!=ESP_OK) return false;
        esp_pm_sleep_cbs_register_config_t sleepCallbacks{};
        sleepCallbacks.exit_cb=[](int64_t us,void *arg)->esp_err_t {
            auto &self=*static_cast<Standby *>(arg);
            if (us>0) {
                self.lightSleepCount.fetch_add(1,std::memory_order_relaxed);
                self.lightSleepUs.fetch_add(uint32_t(us),std::memory_order_relaxed);
                // A short touch IRQ may have ended before the CPU resumes.
                // Preserve the hardware wake cause even if no level ISR remains.
                if (esp_sleep_get_wakeup_causes() & (1U << ESP_SLEEP_WAKEUP_GPIO)) {
                    // This S31 SDK exposes a shared GPIO cause, not per-pin
                    // status. A held AI key is handled by its debounce worker;
                    // otherwise preserve a short touch/released-key wake pulse.
                    if (!self.suppressHeldKeyWake.load(std::memory_order_relaxed) &&
                        gpio_get_level(BSP_BUTTON_AI_GPIO)!=BSP_BUTTON_ACTIVE_LEVEL) {
                        self.pending.fetch_or(WakeInput::Touch,std::memory_order_relaxed);
                        xTaskNotifyGive(self.mainTask);
                    }
                    xTaskNotifyGive(self.inputTask);
                }
            }
            return ESP_OK;
        };
        sleepCallbacks.exit_cb_user_arg=this;
        if (esp_pm_light_sleep_register_cbs(&sleepCallbacks)!=ESP_OK) return false;
        if (esp_pm_configure(&pm)!=ESP_OK) return false;
        gpio_intr_enable(BSP_LCD_TOUCH_INT);
        xTaskNotifyGive(inputTask);
        ESP_LOGI("standby","DFS 40-%d MHz; automatic Light-sleep; interrupt wake enabled",
                 CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
        idleSince=esp_timer_get_time();
        return true;
    }

    void poll(const pixel_screen::Snapshot &picture, int64_t now, int idleMinutes) {
        // ESP-Iris owns USB. Observe its TinyUSB state without changing the
        // transport: a mounted, active host must not lose the PHY to sleep.
        const bool hostActive=tud_mounted() && !tud_suspended();
        if (hostActive!=usbHeld) {
            const auto ret=hostActive?esp_pm_lock_acquire(usbLock):esp_pm_lock_release(usbLock);
            if (ret==ESP_OK) usbHeld=hostActive;
            else ESP_LOGW("standby","USB power lock failed: %s",esp_err_to_name(ret));
        }
        const unsigned events=pending.exchange(0,std::memory_order_relaxed);
        if (events & WakeInput::Down) {
            consumeButton=sleeping;longHandled=false;
            if (sleeping) wake("AI button",now);
            else idleSince=now;
        }
        if (events & WakeInput::Touch) {
            if (sleeping) wake("touch",now);
            else idleSince=now;
        }
        if ((events & WakeInput::Long) && !consumeButton) {
            longHandled=true;
            if (!sleeping) sleep(now);
        }
        if (events & WakeInput::Up) {
            if (!consumeButton && !longHandled && !sleeping) bridgeToggleLayout();
            consumeButton=false;longHandled=false;
        }
        const int64_t idleLimit = idleMinutes * 60 * 1000000LL;
        const bool autoIdle = idleMinutes > 0;
        if (!autoIdle && sleeping && autoSleeping) wake("auto sleep disabled",now);
        if (!autoIdle && pixelSuspended) {
            bridgeSuspendPixel(false);pixelSuspended=false;
        }
        using pixel_screen::State;
        const bool printActive = picture.state == State::Preparing ||
                                 picture.state == State::Printing;
        if (sleeping && printActive && !wasPrinting) wake("print started", now);
        wasPrinting = printActive;
        if (printActive) idleSince = now;
        if (autoIdle && !sleeping && !printActive && now - idleSince >= idleLimit)
            sleep(now,false);
        if (autoIdle && sleeping && now - idleSince >= idleLimit && !pixelSuspended) {
            bridgeSuspendPixel(true);
            pixelSuspended = true;
            ESP_LOGI("standby", "External pixel link paused after configured idle timeout");
        }

    }

private:
    bool sleeping = false, pixelSuspended = false, wasPrinting = false;
    bool autoSleeping=false,redrawPending=false;
    bool consumeButton=false,longHandled=false;
    WakeInput input;
    std::atomic<uint32_t> lightSleepCount{0},lightSleepUs{0};
    std::atomic<bool> suppressHeldKeyWake{false};
    TaskHandle_t inputTask=nullptr,mainTask=nullptr;
    esp_pm_lock_handle_t cpuLock=nullptr,awakeLock=nullptr;
    esp_pm_lock_handle_t usbLock=nullptr;
    bool usbHeld=false;
    static void IRAM_ATTR buttonInterrupt(void *arg) {
        auto &self=*static_cast<Standby *>(arg);
        BaseType_t higher=pdFALSE;
        gpio_intr_disable(BSP_BUTTON_AI_GPIO);
        vTaskNotifyGiveFromISR(self.inputTask,&higher);
        if (higher) portYIELD_FROM_ISR();
    }
    static void IRAM_ATTR touchInterrupt(esp_lcd_touch_handle_t,void *arg) {
        auto &self=*static_cast<Standby *>(arg);
        gpio_intr_disable(BSP_LCD_TOUCH_INT);
        self.pending.fetch_or(WakeInput::Touch,std::memory_order_relaxed);
        BaseType_t higher=pdFALSE;
        vTaskNotifyGiveFromISR(self.inputTask,&higher);
        vTaskNotifyGiveFromISR(self.mainTask,&higher);
        if (higher) portYIELD_FROM_ISR();
    }
    static void inputWorker(void *arg) {
        auto &self=*static_cast<Standby *>(arg);
        for (;;) {
            ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
            int64_t releasedAt=0;
            do {
                const bool down=gpio_get_level(BSP_BUTTON_AI_GPIO)==BSP_BUTTON_ACTIVE_LEVEL;
                const int64_t now=esp_timer_get_time();
                const unsigned events=self.input.sample(down,now);
                if (events & WakeInput::Up) self.suppressHeldKeyWake.store(false,std::memory_order_relaxed);
                if (events) {
                    self.pending.fetch_or(events,std::memory_order_relaxed);
                    xTaskNotifyGive(self.mainTask);
                }
                const bool touching=gpio_get_level(BSP_LCD_TOUCH_INT)==0;
                // Level wake shares the GPIO interrupt type. Mask it while low
                // to avoid an ISR storm; re-arm only after release.
                if (!touching) gpio_intr_enable(BSP_LCD_TOUCH_INT);
                if (down || touching) releasedAt=0;
                else if (!releasedAt) releasedAt=now;
                if (releasedAt && now-releasedAt>=40000) {
                    gpio_intr_enable(BSP_BUTTON_AI_GPIO);
                    break;
                }
                ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));
            } while (true);
        }
    }
    static_assert(std::atomic<unsigned>::is_always_lock_free,"Wake ISR requires lock-free event flags");
    std::atomic<unsigned> pending{0};
    int64_t idleSince = 0;
    wifi_ps_type_t priorPowerSave = WIFI_PS_MIN_MODEM;

    void sleep(int64_t now, bool resetIdle = true) {
        suppressHeldKeyWake.store(gpio_get_level(BSP_BUTTON_AI_GPIO)==BSP_BUTTON_ACTIVE_LEVEL,
                                  std::memory_order_relaxed);
        const auto ret=bsp_display_off();
        if (ret!=ESP_OK) {ESP_LOGE("standby","Display off failed: %s",esp_err_to_name(ret));return;}
        sleeping = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_set_worker_idle_ms(250));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_lock_release(cpuLock));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_lock_release(awakeLock));
        autoSleeping=!resetIdle;
        if (resetIdle) idleSince = now;
        if (esp_wifi_get_ps(&priorPowerSave) == ESP_OK)
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(WIFI_PS_MAX_MODEM));
        ESP_LOGI("standby", "Display off; MQTT remains available for print wake");
    }

    void wake(const char *reason, int64_t now) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_lock_acquire(awakeLock));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_pm_lock_acquire(cpuLock));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_set_worker_idle_ms(10));
        const auto ret=bsp_display_on();
        if (ret!=ESP_OK) {
            esp_pm_lock_release(cpuLock);esp_pm_lock_release(awakeLock);
            ESP_LOGE("standby","Wake failed (%s): %s",reason,esp_err_to_name(ret));return;
        }
        if (bsp_display_lock(200)) {
            // QSPI resume does not invalidate the cached LVGL canvas itself.
            // Sleep may discard panel RAM even when rendered pixels are unchanged.
            lv_obj_invalidate(lv_screen_active());
            if (!strcmp(reason,"touch")) {
                if (auto *indev=bsp_display_get_input_dev()) lv_indev_wait_release(indev);
            }
            bsp_display_unlock();
        } else ESP_LOGW("standby","Wake redraw lock timed out");
        sleeping = false;
        autoSleeping=false;
        redrawPending=true;
        idleSince = now;
        pixelSuspended = false;
        bridgeSuspendPixel(false);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_ps(priorPowerSave));
        ESP_LOGI("standby", "Awake: %s", reason);
    }
};

Display display;
Standby standby;
}

extern "C" void app_main() {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    iris_ota_support_start();
    if (!display.start() || !bridgeStart()) return;
    if (!standby.start()) {ESP_LOGE("standby","Wake input initialization failed");return;}
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
        display.battery.poll(standby.asleep(),standby.sleepCount(),standby.sleepMicros(),standby.usbLocked());
        const unsigned frame = now / 100000;
        if (!standby.asleep() && (frame != lastFrame || standby.redrawRequested()) &&
            display.present(picture, frame, standby.redrawRequested())) {
            standby.redrawn();
            lastFrame = frame;
            if (!healthy) healthy = esp_iris_mark_healthy() == ESP_OK;
        }
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(standby.asleep() ? 250 : 50));
    }
}
