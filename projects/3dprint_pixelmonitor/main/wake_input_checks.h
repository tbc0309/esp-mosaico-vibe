#pragma once
#include "wake_input.h"
// Compile-time regression fixtures exercise the real sampler, with no firmware
// test task or runtime cost. A short tap must survive a 100ms application poll.
namespace wake_input_checks {
constexpr bool shortTap() {
    WakeInput input;
    unsigned pending=input.sample(true,0);
    pending|=input.sample(true,30000);
    pending|=input.sample(false,60000);
    pending|=input.sample(false,90000);
    return pending==(WakeInput::Down|WakeInput::Up);
}
constexpr bool longPressOnce() {
    WakeInput input;
    input.sample(true,0);
    if (input.sample(true,30000)!=WakeInput::Down) return false;
    if (input.sample(true,2029999)!=0) return false;
    if (input.sample(true,2030000)!=WakeInput::Long) return false;
    if (input.sample(true,2100000)!=0) return false;
    if (input.sample(false,2110000)!=0) return false;
    return input.sample(false,2140000)==WakeInput::Up;
}
constexpr bool releaseBeforeLong() {
    WakeInput input;
    input.sample(true,0);input.sample(true,30000);
    if (input.sample(false,2020000)!=0) return false;
    return input.sample(false,2050000)==WakeInput::Up;
}
constexpr bool contactBounce() {
    WakeInput input;
    if (input.sample(true,0)!=0 || input.sample(false,10000)!=0 ||
        input.sample(true,20000)!=0 || input.sample(true,49999)!=0) return false;
    if (input.sample(true,50000)!=WakeInput::Down) return false;
    if (input.sample(false,60000)!=0 || input.sample(true,70000)!=0 ||
        input.sample(false,80000)!=0) return false;
    return input.sample(false,110000)==WakeInput::Up;
}
static_assert(shortTap(),"Short wake tap lost between application polls");
static_assert(longPressOnce(),"Long press must fire once at two seconds");
static_assert(releaseBeforeLong(),"Debounced release must not turn into a long press");
static_assert(contactBounce(),"Contact bounce must produce one press and release");
}
