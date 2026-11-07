#pragma once
#include <cstdint>

// Independent of LVGL. One interrupt-driven worker samples only during key activity.
class WakeInput {
public:
    enum Event : unsigned { Down=1, Up=2, Long=4, Touch=8 };
    constexpr unsigned sample(bool button, int64_t now) {
        unsigned events=0;
        if (button!=raw) {raw=button;changedAt=now;}
        if (button!=pressed && now-changedAt>=30000) {
            pressed=button;
            if (pressed) {pressedAt=now;longSent=false;events|=Down;}
            else events|=Up;
        }
        if (button && pressed && !longSent && now-pressedAt>=2000000) {
            longSent=true;events|=Long;
        }
        return events;
    }
private:
    bool raw=false,pressed=false,longSent=false;
    int64_t changedAt=0,pressedAt=0;
};
