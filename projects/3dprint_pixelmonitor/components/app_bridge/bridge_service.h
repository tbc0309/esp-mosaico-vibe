#pragma once
#include <string>
#include "ported/features/pixel_screen/core.h"
// Start once and poll on the same application task.
bool bridgeStart();
pixel_screen::Snapshot bridgePoll();
std::string bridgeAddress();
bool bridgeWifiOnline();

struct BridgeWifiState {
    bool online;
    bool scanning;
    std::string phase, error, networks;
};
BridgeWifiState bridgeWifiState();
bool bridgeWifiScan();
std::string bridgeWifiConnect(const char *ssid, const char *password);

bool bridgeToggleLayout();
void bridgeSuspendPixel(bool suspended);

struct BoardDisplaySettings { int brightness, idleMinutes; };
BoardDisplaySettings bridgeDisplaySettings();

// Diagnostic snapshot copied under the bridge lock; HTTP never accesses I2C.
struct BridgeBatterySample {
    uint32_t uptimeMs=0,lightSleepCount=0,lightSleepUs=0;
    bool displaySleeping=false;
    bool usbMounted=false,usbSuspended=false,usbSleepLock=false;
    int soc=-1, voltageMv=0, currentMa=0, averageMa=0, remainingMah=0,
        fullCapacityMah=0, flags=0, timeToFull=0, gauging=-1;
};
void bridgePublishBatterySample(const BridgeBatterySample &sample);
