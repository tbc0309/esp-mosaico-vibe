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
