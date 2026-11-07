#pragma once
#include <cstdint>

namespace bridge {
enum class BootAction { None, WifiReset, FactoryReset };
constexpr BootAction bootAction(uint32_t heldMs) {
    return heldMs >= 10000 ? BootAction::FactoryReset
         : heldMs >= 3000 ? BootAction::WifiReset : BootAction::None;
}

// GPIO levels must remain stable for 40 ms. Only release commits an action.
class BootButtonPolicy {
    bool raw_ = false, stable_ = false, armed_ = false;
    uint32_t changedAt_ = 0, pressedAt_ = 0;
public:
    BootAction update(bool pressed, uint32_t now) {
        if (pressed != raw_) { raw_ = pressed; changedAt_ = now; }
        if (uint32_t(now - changedAt_) < 40) return BootAction::None;
        if (!raw_ && !stable_) { armed_ = true; return BootAction::None; }
        if (raw_ == stable_) return BootAction::None;
        stable_ = raw_;
        if (stable_) { if (armed_) pressedAt_ = changedAt_; return BootAction::None; }
        auto action = armed_ ? bootAction(uint32_t(changedAt_ - pressedAt_)) : BootAction::None;
        armed_ = true;
        return action;
    }
    uint32_t heldMs(uint32_t now) const { return stable_ && armed_ ? uint32_t(now - pressedAt_) : 0; }
};
}
