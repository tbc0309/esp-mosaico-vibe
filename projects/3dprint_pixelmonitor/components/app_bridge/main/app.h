#pragma once
#include "ported/platform/idf/json.h"
#include "ported/features/pixel_screen/core.h"
#include <mutex>
#include <string>
#include <atomic>

namespace bridge {
constexpr size_t configLimit=262144;
extern std::recursive_mutex guard;
extern DynamicJsonDocument config;
extern std::atomic<uint32_t> revision;
extern std::atomic<bool> wifiOnline;
extern std::atomic<bool> printerOnline;
extern std::atomic<int> lastRssi;
bool configBegin();
bool configSave(JsonVariantConst input,std::string &error);
bool configRestore(JsonVariantConst input,std::string &error);
bool preferencesSave(JsonVariantConst input,std::string &error);
void preferencesPublic(JsonObject output);
bool provisionWifi(JsonVariantConst input,std::string &error);
void configPublic(JsonObject output);
void configBackup(JsonObject output);
void configCopy(DynamicJsonDocument &output);
std::string secret(JsonVariantConst node,const char *key,const std::string &binding);
bool adminVerify(const std::string &password);
bool adminChange(const std::string &password,bool initial=false);
bool adminPasswordChangeRequired();
void networkBegin();
void networkPoll();
void networkStatus(JsonObject output);
bool provisioningRequest(int socket,bool write=false);
void networkApply();
void networkRetry();
bool wifiScan();
void wifiScanStatus(JsonObject output);
void setupMode(bool enabled);
extern std::atomic<bool> printerProbeRequested,printerProbePaused;
void lanValidate(JsonVariantConst input,JsonObject result);
void printerBegin();
void printerPoll();
void printerStatus(JsonObject output);
void telemetryReset(const std::string &model);
void telemetryReport(JsonObjectConst report);
pixel_screen::Snapshot telemetrySnapshot();
void pixelApply();
pixel_screen::Snapshot pixelPoll();
void pixelStatus(JsonObject output);
void webBegin();
void updatePoll();
void updateStatus(JsonObject output);
bool updateRequest(bool install,const std::string &version,std::string &error);
void serialBegin();
bool configReset(bool all);
void bootButtonBegin();
bool bootButtonFeedback();
bool cloudStart(JsonVariantConst request,bool internal=false);
bool cloudBackgroundBusy();
void cloudSessionsPoll();
void cloudAccountHealth(const char *id,JsonObject output);
void cloudAccountResult(const char *id,bool okay,bool rejected);
void cloudAccountRejected(const char *id);
void cloudStatus(JsonObject output);
void cloudClear();
void cloudBegin();
std::string ipv4();
std::string modelShort(const std::string &model);
}
