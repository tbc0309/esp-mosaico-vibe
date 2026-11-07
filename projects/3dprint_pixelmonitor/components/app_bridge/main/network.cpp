#include "app.h"
#include "network_policy.h"
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <atomic>
#include <vector>
#include <lwip/sockets.h>
#include "vendor/captive_dns/dns_server.h"

namespace bridge {
namespace {
std::string configuredSsid,configuredPassword,address,phase="starting",lastError;
bool reconnectRequested=false;
dns_server_handle_t portalDns=nullptr;
bool ap=true,connecting=false,sntpStarted=false;unsigned attempts=0;
std::atomic<bool> requestedScan{false},scanning{false},scanDone{false};
uint32_t scanAt=0,connectAt=0,retryAt=0,closeAt=0,seenRevision=UINT32_MAX;
DynamicJsonDocument scanResults(8192);
uint32_t now(){return uint32_t(esp_timer_get_time()/1000);}
void event(void*,esp_event_base_t base,int32_t id,void *data){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED){wifiOnline=false;connecting=false;auto reason=static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
  ESP_LOGW("network","Wi-Fi disconnected, reason=%u",unsigned(reason));
  lastError=reason==WIFI_REASON_NO_AP_FOUND?"wifi_not_found":
   (reason==WIFI_REASON_AUTH_FAIL||reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT||reason==WIFI_REASON_HANDSHAKE_TIMEOUT)?"wifi_auth_failed":"disconnected";phase="retrying";closeAt=0;retryAt=now()+networkRetryDelay(attempts);}
 if(base==WIFI_EVENT&&id==WIFI_EVENT_SCAN_DONE){scanning=false;scanDone=true;}
 if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP){
  auto *e=static_cast<ip_event_got_ip_t*>(data);char text[20];snprintf(text,sizeof(text),IPSTR,IP2STR(&e->ip_info.ip));address=text;wifiOnline=true;connecting=false;attempts=0;phase="connected";lastError.clear();if(ap)closeAt=now()+30000;
  ESP_LOGI("network","Device address: http://%s/",text);
  if(!sntpStarted){esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_setservername(1,"time.cloudflare.com");esp_sntp_init();sntpStarted=true;}
 }
}
}
std::string ipv4(){std::lock_guard<std::recursive_mutex> lock(guard);return wifiOnline?address:"192.168.4.1";}
bool provisioningRequest(int socket,bool write){
 std::lock_guard<std::recursive_mutex> lock(guard);if(!ap||(write&&wifiOnline))return false;
 sockaddr_storage local{};socklen_t size=sizeof(local);
 if(getsockname(socket,reinterpret_cast<sockaddr*>(&local),&size)!=0)return false;
 uint32_t localIp=0;
 if(local.ss_family==AF_INET){
  localIp=reinterpret_cast<const sockaddr_in*>(&local)->sin_addr.s_addr;
 }
#if LWIP_IPV6
 else if(local.ss_family==AF_INET6){
  const auto &ip=reinterpret_cast<const sockaddr_in6*>(&local)->sin6_addr;
  if(!IN6_IS_ADDR_V4MAPPED(&ip))return false;
  memcpy(&localIp,&ip.s6_addr[12],sizeof(localIp));
 }
#endif
 else return false;
 auto *netif=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");esp_netif_ip_info_t info{};
 return netif&&esp_netif_get_ip_info(netif,&info)==ESP_OK&&localIp==info.ip.addr;
}
void portalDnsApply(){
 if(ap&&!portalDns){
  dns_server_config_t dns{};dns.num_of_entries=1;dns.item[0].name="*";dns.item[0].if_key="WIFI_AP_DEF";
  portalDns=start_dns_server(&dns);
 }else if(!ap&&portalDns){stop_dns_server(portalDns);portalDns=nullptr;}
}
void setupMode(bool enabled){std::lock_guard<std::recursive_mutex> lock(guard);if(ap==enabled)return;ap=enabled;closeAt=0;esp_wifi_set_mode(ap?WIFI_MODE_APSTA:WIFI_MODE_STA);}
void networkRetry(){std::lock_guard<std::recursive_mutex> lock(guard);reconnectRequested=true;}
void networkApply(){
 std::lock_guard<std::recursive_mutex> lock(guard);auto w=config["wifi"].as<JsonVariantConst>();std::string ssid=w["ssid"]|"",password=secret(w,"password","wifi");seenRevision=revision.load();
 if(ssid==configuredSsid&&password==configuredPassword&&phase!="starting"&&!reconnectRequested)return;
 reconnectRequested=false;
 configuredSsid=ssid;configuredPassword=password;attempts=0;connecting=false;retryAt=now()+500;lastError.clear();wifiOnline=false;closeAt=0;
 esp_wifi_scan_stop();scanning=false;esp_wifi_disconnect();setupMode(true);
 if(ssid.empty()){phase="setup";return;}
 wifi_config_t c{};memcpy(c.sta.ssid,ssid.data(),std::min(ssid.size(),sizeof(c.sta.ssid)));snprintf(reinterpret_cast<char*>(c.sta.password),sizeof(c.sta.password),"%s",password.c_str());c.sta.threshold.authmode=WIFI_AUTH_OPEN;c.sta.pmf_cfg.capable=true;c.sta.pmf_cfg.required=false;
 if(esp_wifi_set_config(WIFI_IF_STA,&c)!=ESP_OK){lastError="invalid_wifi_config";phase="setup";configuredSsid.clear();return;}phase="connecting";
}
void networkBegin(){
 ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());esp_netif_create_default_wifi_sta();esp_netif_create_default_wifi_ap();
 wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();ESP_ERROR_CHECK(esp_wifi_init(&init));ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
 ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,nullptr));ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,nullptr));
 wifi_config_t c{};uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP);snprintf(reinterpret_cast<char*>(c.ap.ssid),sizeof(c.ap.ssid),"PrintPixel-%02X%02X%02X",mac[3],mac[4],mac[5]);snprintf(reinterpret_cast<char*>(c.ap.password),sizeof(c.ap.password),"12345678");c.ap.ssid_len=strlen(reinterpret_cast<char*>(c.ap.ssid));c.ap.channel=1;c.ap.max_connection=4;c.ap.authmode=WIFI_AUTH_WPA2_PSK;
 ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&c));auto *apIf=esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
 esp_netif_ip_info_t ip{};
 ESP_ERROR_CHECK(esp_netif_get_ip_info(apIf,&ip));
 esp_netif_dns_info_t dns{};dns.ip.type=ESP_IPADDR_TYPE_V4;dns.ip.u_addr.ip4=ip.ip;
 ESP_ERROR_CHECK(esp_netif_set_dns_info(apIf,ESP_NETIF_DNS_MAIN,&dns));
 uint8_t offerDns=2;
 ESP_ERROR_CHECK(esp_netif_dhcps_option(apIf,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offerDns,sizeof(offerDns)));
 ESP_ERROR_CHECK(esp_wifi_start());networkApply();
 ESP_LOGI("network","Setup hotspot %s; http://192.168.4.1/",c.ap.ssid);
}
bool wifiScan(){if(scanning||requestedScan)return false;requestedScan=true;return true;}
void wifiScanStatus(JsonObject out){std::lock_guard<std::recursive_mutex> lock(guard);out["scanning"]=scanning.load()||requestedScan.load();out["networks"]=scanResults.as<JsonArrayConst>();}
void networkStatus(JsonObject out){std::lock_guard<std::recursive_mutex> lock(guard);const bool online=wifiOnline&&!reconnectRequested;out["online"]=online;out["ip"]=online?address:"";out["setup"]=ap;out["phase"]=reconnectRequested?"connecting":phase;out["error"]=lastError;out["attempts"]=attempts;out["ssid"]=configuredSsid;wifi_ap_record_t record{};if(wifiOnline&&esp_wifi_sta_get_ap_info(&record)==ESP_OK){lastRssi=record.rssi;out["rssi"]=record.rssi;}}
void networkPoll(){
 std::lock_guard<std::recursive_mutex> lock(guard);portalDnsApply();uint32_t tick=now();if(reconnectRequested||seenRevision!=revision.load())networkApply();
 if(requestedScan.exchange(false)){
  if(connecting){esp_wifi_disconnect();connecting=false;}scanResults.to<JsonArray>();auto e=esp_wifi_scan_start(nullptr,false);if(e==ESP_OK){scanning=true;scanAt=tick;}else lastError="scan_failed";
 }
 if(scanning&&uint32_t(tick-scanAt)>15000){esp_wifi_scan_stop();scanning=false;lastError="scan_timeout";}
 if(scanDone.exchange(false)){
  uint16_t count=24;std::vector<wifi_ap_record_t> records(count);if(esp_wifi_scan_get_ap_records(&count,records.data())==ESP_OK){auto list=scanResults.to<JsonArray>();for(unsigned i=0;i<count;i++){auto &v=records[i];if(!v.ssid[0])continue;bool duplicate=false;for(JsonObjectConst x:list)if(!strcmp(x["ssid"]|"",reinterpret_cast<char*>(v.ssid)))duplicate=true;if(duplicate)continue;auto o=list.createNestedObject();o["ssid"]=reinterpret_cast<char*>(v.ssid);o["rssi"]=v.rssi;o["secure"]=v.authmode!=WIFI_AUTH_OPEN;}}
 }
 if(!wifiOnline&&!connecting&&!scanning&&!configuredSsid.empty()&&networkDue(tick,retryAt)){if(attempts<5)++attempts;else{phase="retrying";setupMode(true);}connectAt=tick;connecting=esp_wifi_connect()==ESP_OK;if(!connecting)retryAt=tick+networkRetryDelay(attempts);}
 if(connecting&&uint32_t(tick-connectAt)>20000){esp_wifi_disconnect();connecting=false;retryAt=tick+networkRetryDelay(attempts);lastError="connect_timeout";}
 if(ap&&wifiOnline&&closeAt&&int32_t(tick-closeAt)>=0)setupMode(false);
}
}
