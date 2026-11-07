#include "app.h"
#include "board.h"
#include "bridge_service.h"

#include "ported/features/pixel_screen/connection.h"
#include "ported/platform/idf/ble_runtime.h"
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <memory>
#include <vector>
#include <algorithm>

namespace bridge {
namespace {
httpd_handle_t server=nullptr;
BridgeBatterySample batterySample;
struct AdminSession{std::string token;uint32_t expiresAt;};
std::vector<AdminSession> sessions;
uint32_t loginAt=0;unsigned attempts=0;
extern const uint8_t htmlStart[] asm("_binary_index_html_gz_start");extern const uint8_t htmlEnd[] asm("_binary_index_html_gz_end");
extern const uint8_t appStart[] asm("_binary_app_js_gz_start");extern const uint8_t appEnd[] asm("_binary_app_js_gz_end");
extern const uint8_t cssStart[] asm("_binary_style_css_gz_start");extern const uint8_t cssEnd[] asm("_binary_style_css_gz_end");
extern const uint8_t artStart[] asm("_binary_pixel_art_js_gz_start");extern const uint8_t artEnd[] asm("_binary_pixel_art_js_gz_end");
extern const uint8_t localesStart[] asm("_binary_locales_js_gz_start");extern const uint8_t localesEnd[] asm("_binary_locales_js_gz_end");
extern const uint8_t i18nStart[] asm("_binary_i18n_js_gz_start");extern const uint8_t i18nEnd[] asm("_binary_i18n_js_gz_end");
extern const uint8_t formsStart[] asm("_binary_forms_js_gz_start");extern const uint8_t formsEnd[] asm("_binary_forms_js_gz_end");
extern const uint8_t zonesStart[] asm("_binary_timezones_js_gz_start");extern const uint8_t zonesEnd[] asm("_binary_timezones_js_gz_end");
extern const uint8_t pickerStart[] asm("_binary_timezone_picker_js_gz_start");extern const uint8_t pickerEnd[] asm("_binary_timezone_picker_js_gz_end");
extern const uint8_t managerStart[] asm("_binary_printer_manager_js_gz_start");extern const uint8_t managerEnd[] asm("_binary_printer_manager_js_gz_end");
extern const uint8_t updatesStart[] asm("_binary_online_update_js_gz_start");extern const uint8_t updatesEnd[] asm("_binary_online_update_js_gz_end");
uint32_t now(){return uint32_t(esp_timer_get_time()/1000);}
esp_err_t send(httpd_req_t *r,JsonDocument &doc){std::string text;serializeJson(doc,text);httpd_resp_set_type(r,"application/json; charset=utf-8");httpd_resp_set_hdr(r,"Cache-Control","no-store");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");return httpd_resp_send(r,text.data(),text.size());}
esp_err_t error(httpd_req_t *r,int status,const char *message){httpd_resp_set_status(r,status==401?"401 Unauthorized":status==403?"403 Forbidden":status==409?"409 Conflict":status==413?"413 Payload Too Large":status==429?"429 Too Many Requests":"400 Bad Request");StaticJsonDocument<256> d;d["error"]=message;return send(r,d);}
std::string bearer(httpd_req_t *r){char header[96]{};if(httpd_req_get_hdr_value_str(r,"Authorization",header,sizeof(header))!=ESP_OK||strncmp(header,"Bearer ",7))return {};return header+7;}
bool sameToken(const std::string &a,const std::string &b){uint8_t difference=uint8_t(a.size()!=b.size());for(size_t i=0;i<std::min(a.size(),b.size());++i)difference|=uint8_t(a[i]^b[i]);return difference==0;}
bool authorized(httpd_req_t *r){
 uint32_t tick=now();sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[&](const AdminSession &s){return int32_t(tick-s.expiresAt)>=0;}),sessions.end());
 auto token=bearer(r);if(token.empty())return false;for(const auto &session:sessions)if(sameToken(session.token,token))return true;return false;
}
bool body(httpd_req_t *r,JsonDocument &doc,size_t limit=196608){if(r->content_len<=0||size_t(r->content_len)>limit)return false;std::string text;text.resize(r->content_len);size_t at=0;while(at<text.size()){int n=httpd_req_recv(r,text.data()+at,text.size()-at);if(n<=0)return false;at+=n;}return deserializeJson(doc,text)==DeserializationError::Ok;}
void rebootTask(void*){vTaskDelay(pdMS_TO_TICKS(1500));esp_restart();}
esp_err_t page(httpd_req_t *r){const uint8_t *a=htmlStart,*b=htmlEnd;const char *type="text/html; charset=utf-8";if(!strcmp(r->uri,"/app.js")){a=appStart;b=appEnd;type="text/javascript";}if(!strcmp(r->uri,"/pixel-art.js")){a=artStart;b=artEnd;type="text/javascript";}if(!strcmp(r->uri,"/style.css")){a=cssStart;b=cssEnd;type="text/css";}
 if(!strcmp(r->uri,"/online-update.js")){a=updatesStart;b=updatesEnd;type="text/javascript";}
 if(!strcmp(r->uri,"/locales.js")){a=localesStart;b=localesEnd;type="text/javascript";}if(!strcmp(r->uri,"/i18n.js")){a=i18nStart;b=i18nEnd;type="text/javascript";}
 if(!strcmp(r->uri,"/forms.js")){a=formsStart;b=formsEnd;type="text/javascript";}
 if(!strcmp(r->uri,"/timezones.js")){a=zonesStart;b=zonesEnd;type="text/javascript";}if(!strcmp(r->uri,"/timezone-picker.js")){a=pickerStart;b=pickerEnd;type="text/javascript";}
 if(!strcmp(r->uri,"/printer-manager.js")){a=managerStart;b=managerEnd;type="text/javascript";}
 httpd_resp_set_type(r,type);httpd_resp_set_hdr(r,"Content-Encoding","gzip");httpd_resp_set_hdr(r,"Cache-Control","no-cache");httpd_resp_set_hdr(r,"X-Frame-Options","DENY");httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'");return httpd_resp_send(r,reinterpret_cast<const char*>(a),b-a);
}
esp_err_t get(httpd_req_t *r){
 if(!strcmp(r->uri,"/api/health")){StaticJsonDocument<512> d;d["boardName"]=board::name;d["project"]="3dprint_pixelmonitor";d["version"]=esp_app_get_description()->version;d["provisioning"]=provisioningRequest(httpd_req_to_sockfd(r));preferencesPublic(d.as<JsonObject>());return send(r,d);}
 if(!strcmp(r->uri,"/api/provision")){
  if(!provisioningRequest(httpd_req_to_sockfd(r)))return error(r,403,"setup_unavailable");
  StaticJsonDocument<1024> d;networkStatus(d.createNestedObject("network"));return send(r,d);
 }
 if(!strcmp(r->uri,"/api/wifi/scan")&&provisioningRequest(httpd_req_to_sockfd(r))){DynamicJsonDocument d(8192);wifiScanStatus(d.to<JsonObject>());return send(r,d);}
 if(!authorized(r))return error(r,401,"login_required");if(adminPasswordChangeRequired())return error(r,403,"password_change_required");bool configuration=!strcmp(r->uri,"/api/config")||!strcmp(r->uri,"/api/backup");DynamicJsonDocument d(configuration?configLimit:!strcmp(r->uri,"/api/cloud")?65536:16384);
 if(!strcmp(r->uri,"/api/battery")){
  std::lock_guard<std::recursive_mutex> lock(guard);const auto &s=batterySample;
  d["uptimeMs"]=s.uptimeMs;d["soc"]=s.soc;d["voltageMv"]=s.voltageMv;d["currentMa"]=s.currentMa;
  d["averageMa"]=s.averageMa;d["remainingMah"]=s.remainingMah;d["fullCapacityMah"]=s.fullCapacityMah;
  d["displaySleeping"]=s.displaySleeping;d["lightSleepCount"]=s.lightSleepCount;d["lightSleepUs"]=s.lightSleepUs;
  d["resetReason"]=int(esp_reset_reason());
  d["usbMounted"]=s.usbMounted;d["usbSuspended"]=s.usbSuspended;d["usbSleepLock"]=s.usbSleepLock;
  d["flags"]=s.flags;d["timeToFull"]=s.timeToFull;d["gauging"]=s.gauging;
  d["full"]=bool(s.flags&0x200);d["tca"]=bool(s.flags&0x40);d["chargeMode"]=s.gauging<0?false:!(s.gauging&0x40);
 }
 else if(!strcmp(r->uri,"/api/config"))configPublic(d.to<JsonObject>());
 else if(!strcmp(r->uri,"/api/backup")){configBackup(d.to<JsonObject>());httpd_resp_set_hdr(r,"Content-Disposition","attachment; filename=3dprint_pixelmonitor-backup.json");}
 else if(!strcmp(r->uri,"/api/status")){d["project"]="3dprint_pixelmonitor";d["version"]=esp_app_get_description()->version;d["uptime"]=now()/1000;d["board"]["id"]=board::id;d["board"]["name"]=board::name;d["board"]["target"]=board::target;updateStatus(d.createNestedObject("onlineUpdate"));{std::lock_guard<std::recursive_mutex> lock(guard);auto accounts=d.createNestedArray("accounts");for(JsonObjectConst a:config["accounts"].as<JsonArrayConst>()){auto item=accounts.createNestedObject();item["id"]=a["id"];cloudAccountHealth(a["id"]|"",item);}}networkStatus(d.createNestedObject("network"));printerStatus(d.createNestedObject("printer"));pixelStatus(d.createNestedObject("pixel"));xtouch_ble::status(d.createNestedObject("bluetooth"));d["memory"]["internalFree"]=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);d["memory"]["internalLargest"]=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);d["memory"]["internalMinimum"]=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);d["memory"]["psramFree"]=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);d["ota"]["running"]=esp_ota_get_running_partition()->address;auto *next=esp_ota_get_next_update_partition(nullptr);d["ota"]["next"]=next?next->address:0;}
 else if(!strcmp(r->uri,"/api/wifi/scan"))wifiScanStatus(d.to<JsonObject>());
 else if(!strcmp(r->uri,"/api/pixel/scan"))pixel_screen::connectionScanStatus(d.to<JsonObject>());
 else if(!strcmp(r->uri,"/api/cloud")||!strcmp(r->uri,"/api/printer/validate"))cloudStatus(d.to<JsonObject>());
 else return error(r,400,"unknown_api");return send(r,d);
}

esp_err_t post(httpd_req_t *r){

 if(!strcmp(r->uri,"/api/provision")||(!strcmp(r->uri,"/api/wifi/scan")&&provisioningRequest(httpd_req_to_sockfd(r)))){
  if(!provisioningRequest(httpd_req_to_sockfd(r),true))return error(r,403,"setup_unavailable");
  StaticJsonDocument<512> input,output;if(!body(r,input,512))return error(r,400,"invalid_json");
  if(!strcmp(r->uri,"/api/provision")){std::string reason;if(!provisionWifi(input,reason))return error(r,400,reason.c_str());}
  else if(!wifiScan())return error(r,409,"scan_busy");output["ok"]=true;return send(r,output);
 }
 bool login=!strcmp(r->uri,"/api/login");if(!login&&!authorized(r))return error(r,401,"login_required");if(!login&&adminPasswordChangeRequired()&&strcmp(r->uri,"/api/password")&&strcmp(r->uri,"/api/logout")&&strcmp(r->uri,"/api/preferences"))return error(r,403,"password_change_required");DynamicJsonDocument input(configLimit),output(1024);if(!body(r,input))return error(r,400,"invalid_json");
 if(login){if(uint32_t(now()-loginAt)>60000){loginAt=now();attempts=0;}if(++attempts>10)return error(r,429,"try_later");if(!input["password"].is<const char*>()||!adminVerify(input["password"].as<const char*>()))return error(r,401,"wrong_password");char token[65];for(unsigned i=0;i<8;i++)snprintf(token+i*8,9,"%08lx",(unsigned long)esp_random());if(sessions.size()>=4)sessions.erase(sessions.begin());sessions.push_back({token,now()+12*60*60*1000});output["token"]=std::string(token);output["passwordChangeRequired"]=adminPasswordChangeRequired();preferencesPublic(output.createNestedObject("preferences"));attempts=0;}
 else if(!strcmp(r->uri,"/api/config")){std::string reason;if(!configSave(input,reason))return error(r,400,reason.c_str());output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/preferences")){std::string reason;if(!preferencesSave(input,reason))return error(r,400,reason.c_str());preferencesPublic(output.to<JsonObject>());}
 else if(!strcmp(r->uri,"/api/restore")){std::string reason;if(!configRestore(input,reason))return error(r,400,reason.c_str());output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/logout")){auto token=bearer(r);sessions.erase(std::remove_if(sessions.begin(),sessions.end(),[&](const AdminSession &s){return sameToken(s.token,token);}),sessions.end());output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/password")){if(!strcmp(input["password"]|"","12345678"))return error(r,400,"password_default");if(adminPasswordChangeRequired()&&(!input["confirmation"].is<const char*>()||strcmp(input["password"]|"",input["confirmation"]|"")))return error(r,400,"password_mismatch");if(!input["password"].is<const char*>()||!adminChange(input["password"].as<const char*>()))return error(r,400,"password_length");sessions.clear();output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/wifi/scan")){if(!wifiScan())return error(r,409,"scan_busy");output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/pixel/scan")){if(!pixel_screen::connectionScanStart())return error(r,409,"scan_busy");output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/printer/validate")){if(cloudBackgroundBusy())return error(r,409,"busy");input["action"]="validate_lan";input["region"]="cn";if(!cloudStart(input))return error(r,409,"cloud_busy_or_invalid");output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/cloud")){if(cloudBackgroundBusy())return error(r,409,"busy");if(!cloudStart(input))return error(r,409,"cloud_busy_or_invalid");output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/cloud/clear")){cloudClear();output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/setup")){if(!wifiOnline&&!input["enabled"].as<bool>())return error(r,409,"no_network");setupMode(input["enabled"]|false);output["ok"]=true;}
 else if(!strcmp(r->uri,"/api/update/check")||!strcmp(r->uri,"/api/update/install")){
  bool installing=!strcmp(r->uri,"/api/update/install");
  if(installing&&input["confirm"]!=true)return error(r,400,"confirmation_required");
  std::string reason;if(!updateRequest(installing,input["version"]|"",reason))return error(r,409,reason.c_str());output["ok"]=true;
 }
 else if(!strcmp(r->uri,"/api/restart")){output["ok"]=true;auto sent=send(r,output);xTaskCreate(rebootTask,"restart",2048,nullptr,1,nullptr);return sent;}
 else return error(r,400,"unknown_api");return send(r,output);
}
}
esp_err_t portalRedirect(httpd_req_t *request,httpd_err_code_t){
 if(request->method!=HTTP_GET||!provisioningRequest(httpd_req_to_sockfd(request)))
  return httpd_resp_send_err(request,HTTPD_404_NOT_FOUND,"Not found");
 httpd_resp_set_status(request,"302 Found");
 httpd_resp_set_hdr(request,"Location","http://192.168.4.1/");
 httpd_resp_set_hdr(request,"Cache-Control","no-store");
 return httpd_resp_sendstr(request,"Open Wi-Fi setup");
}
void webBegin(){httpd_config_t c=HTTPD_DEFAULT_CONFIG();c.max_uri_handlers=48;c.stack_size=12288;c.lru_purge_enable=true;c.recv_wait_timeout=15;c.send_wait_timeout=15;ESP_ERROR_CHECK(httpd_start(&server,&c));
 ESP_ERROR_CHECK(httpd_register_err_handler(server,HTTPD_404_NOT_FOUND,portalRedirect));
 for(const char *path:{"/hotspot-detect.html","/library/test/success.html","/generate_204","/gen_204","/connecttest.txt","/ncsi.txt"}){
  httpd_uri_t probe{};probe.uri=path;probe.method=HTTP_GET;
  probe.handler=[](httpd_req_t *r){return portalRedirect(r,HTTPD_404_NOT_FOUND);};
  ESP_ERROR_CHECK(httpd_register_uri_handler(server,&probe));
 }
 for(const char *path:{"/","/printer-manager.js","/app.js","/style.css","/pixel-art.js","/locales.js","/i18n.js","/forms.js","/timezones.js","/timezone-picker.js","/online-update.js"}){httpd_uri_t u{};u.uri=path;u.method=HTTP_GET;u.handler=page;ESP_ERROR_CHECK(httpd_register_uri_handler(server,&u));}
 for(const char *path:{"/api/health","/api/config","/api/backup","/api/status","/api/battery","/api/wifi/scan","/api/pixel/scan","/api/cloud","/api/printer/validate","/api/provision"}){httpd_uri_t u{};u.uri=path;u.method=HTTP_GET;u.handler=get;ESP_ERROR_CHECK(httpd_register_uri_handler(server,&u));}
 for(const char *path:{"/api/login","/api/logout","/api/config","/api/restore","/api/password","/api/wifi/scan","/api/pixel/scan","/api/cloud","/api/cloud/clear","/api/setup","/api/restart","/api/printer/validate","/api/update/check","/api/update/install","/api/provision","/api/preferences"}){httpd_uri_t u{};u.uri=path;u.method=HTTP_POST;u.handler=post;ESP_ERROR_CHECK(httpd_register_uri_handler(server,&u));}
}
}
void bridgePublishBatterySample(const BridgeBatterySample &sample) {
 std::lock_guard<std::recursive_mutex> lock(bridge::guard);
 bridge::batterySample=sample;
}
