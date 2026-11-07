#include "app.h"
#include "ported/features/pixel_screen/display_policy.h"
#include <ctime>
#include "ported/xtouch/vault.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <psa/crypto.h>
#include <esp_random.h>
#include <algorithm>
#include <cctype>
#include <vector>

namespace bridge {
namespace {
uint8_t adminSalt[16]{},adminHash[32]{};
bool mustChangePassword=true;
bool hashPassword(const std::string &password,uint8_t *out){
 std::vector<uint8_t> data(adminSalt,adminSalt+sizeof(adminSalt));data.insert(data.end(),password.begin(),password.end());size_t size=0;
 return psa_crypto_init()==PSA_SUCCESS&&psa_hash_compute(PSA_ALG_SHA_256,data.data(),data.size(),out,32,&size)==PSA_SUCCESS&&size==32;
}
bool field(JsonVariantConst v,size_t maximum){return v.is<const char*>()&&strlen(v.as<const char*>())<=maximum;}
bool nameValid(const char *s){if(!s||!*s||strlen(s)>40)return false;for(;*s;s++)if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='_')return false;return true;}
JsonVariantConst previous(JsonArrayConst array,const char *id){for(JsonVariantConst v:array)if(!strcmp(v["id"]|"",id))return v;return {};}
bool mergeSecret(JsonObject to,JsonVariantConst input,JsonVariantConst old,const char *key,const std::string &binding,size_t maximum){
 std::string encrypted=std::string(key)+"Enc",policy=std::string(key)+"Encrypted",clear="clear"+std::string(key);clear[5]=toupper(clear[5]);
 if(input.containsKey(policy.c_str())&&!input[policy.c_str()].is<bool>())return false;
 bool protect=input[policy.c_str()]|(old[policy.c_str()]|true);to[policy]=protect;
 if(input.containsKey(clear.c_str())&&input[clear.c_str()].is<bool>()&&input[clear.c_str()].as<bool>()){to.remove(encrypted.c_str());to.remove(key);return true;}
 if(input.containsKey(key)&&!field(input[key],maximum))return false;
 const char *plain=input[key]|"";
 if(!*plain&&protect==(old[policy.c_str()]|true)){
  if(old[encrypted.c_str()].is<const char*>()){to[encrypted]=old[encrypted.c_str()];return true;}
  if(!protect&&old[key].is<const char*>()){to[key]=old[key];return true;}
 }
 std::string value=plain;if(value.empty())value=secret(old,key,binding);
 if(!value.empty()){if(protect){String sealed=xtouch_vault_seal(value.c_str(),binding.c_str());if(sealed.isEmpty())return false;to[encrypted]=sealed;}else to[key]=value;}
 std::fill(value.begin(),value.end(),0);
 return true;
}
void hideSecret(JsonObject obj,const char *key){std::string enc=std::string(key)+"Enc",saved=std::string(key)+"Saved",policy=std::string(key)+"Encrypted";obj[saved]=obj[enc].is<const char*>()||(obj[key].is<const char*>()&&strlen(obj[key].as<const char*>())>0);obj[policy]=obj[policy]|true;obj.remove(enc);obj.remove(key);}
}
bool adminPasswordChangeRequired(){std::lock_guard<std::recursive_mutex> lock(guard);return mustChangePassword;}
bool adminChange(const std::string &password,bool initial){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(password.size()<8||password.size()>64||(!initial&&password=="12345678"))return false;uint8_t next[32];if(!hashPassword(password,next))return false;nvs_handle_t h;
 if(nvs_open("matrix-bridge",NVS_READWRITE,&h)!=ESP_OK)return false;bool okay=nvs_set_blob(h,"admin",next,32)==ESP_OK&&nvs_set_u8(h,"admin_changed",initial?0:1)==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);if(okay){memcpy(adminHash,next,32);mustChangePassword=initial;}memset(next,0,32);return okay;
}
bool adminVerify(const std::string &password){std::lock_guard<std::recursive_mutex> lock(guard);uint8_t check[32]{};if(password.size()>64||!hashPassword(password,check))return false;uint8_t difference=0;for(size_t i=0;i<32;i++)difference|=check[i]^adminHash[i];memset(check,0,32);return difference==0;}
bool configBegin(){
 auto e=nvs_flash_init();if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){if(nvs_flash_erase()!=ESP_OK)return false;e=nvs_flash_init();}if(e!=ESP_OK||!xtouch_vault_begin())return false;
 nvs_handle_t h;if(nvs_open("matrix-bridge",NVS_READWRITE,&h)!=ESP_OK)return false;size_t size=sizeof(adminSalt);
 if(nvs_get_blob(h,"salt",adminSalt,&size)!=ESP_OK||size!=sizeof(adminSalt)){esp_fill_random(adminSalt,sizeof(adminSalt));nvs_set_blob(h,"salt",adminSalt,sizeof(adminSalt));nvs_commit(h);}
 size=32;bool hasAdmin=nvs_get_blob(h,"admin",adminHash,&size)==ESP_OK&&size==32;
 size=0;bool loaded=false;bool blob=nvs_get_blob(h,"config",nullptr,&size)==ESP_OK;if(!blob){size=0;nvs_get_str(h,"config",nullptr,&size);}if(size>0&&size<=configLimit){std::vector<char> value(size+1,0);auto result=blob?nvs_get_blob(h,"config",value.data(),&size):nvs_get_str(h,"config",value.data(),&size);if(result==ESP_OK)loaded=deserializeJson(config,static_cast<const char*>(value.data()))==DeserializationError::Ok;}
 uint8_t changed=0;bool hasMarker=nvs_get_u8(h,"admin_changed",&changed)==ESP_OK;
 nvs_close(h);if(!hasAdmin&&!adminChange("12345678",true))return false;
 mustChangePassword=!hasAdmin||(hasMarker?!changed:adminVerify("12345678"));
 if(!loaded){config.to<JsonObject>();config["schema"]=1;config["activePrinter"]=0;config["timezone"]="";config["timezoneName"]="";config["language"]="";config.createNestedArray("printers");config.createNestedArray("accounts");config.createNestedObject("wifi");auto pixel=config.createNestedObject("pixel");pixel["enabled"]=false;pixel["mac"]="";pixel["brightness"]=80;pixel["screenOn"]=true;pixel["rotate180"]=false;pixel["layout"]="printer";pixel["idleDisplay"]="clock";}
 if(!config["onlineOTA"].is<bool>())config["onlineOTA"]=true;
 if(!config["language"].is<const char*>())config["language"]="";
 const char *zone=config["timezone"]|"";config["project"]="3dprint_pixelmonitor";setenv("TZ",*zone?zone:"CST-8",1);tzset();return true;
}
std::string secret(JsonVariantConst obj,const char *key,const std::string &binding){std::string policy=std::string(key)+"Encrypted";if(obj[policy.c_str()]==false&&obj[key].is<const char*>())return obj[key].as<const char*>();String plain;std::string name=std::string(key)+"Enc";xtouch_vault_open(obj[name.c_str()]|"",binding.c_str(),plain);return plain.c_str();}
void configCopy(DynamicJsonDocument &out){std::lock_guard<std::recursive_mutex> lock(guard);out.set(config);}
void configBackup(JsonObject out){std::lock_guard<std::recursive_mutex> lock(guard);out.set(config.as<JsonObjectConst>());}
void configPublic(JsonObject out){
 std::lock_guard<std::recursive_mutex> lock(guard);out.set(config.as<JsonObjectConst>());hideSecret(out["wifi"],"password");hideSecret(out["pixel"],"password");for(JsonObject p:out["printers"].as<JsonArray>())hideSecret(p,"accessCode");for(JsonObject a:out["accounts"].as<JsonArray>()){a["account"]=a["account"]|"";a["loginMethod"]=a["loginMethod"]|"token";a["loginCleared"]=a["loginCleared"]|false;hideSecret(a,"token");hideSecret(a,"password");cloudAccountHealth(a["id"]|"",a);}
}
static bool persistConfig(const JsonDocument &next,std::string &error){
 if(next.overflowed()){error="config_too_large";return error="config_too_large",false;}std::string encoded;serializeJson(next,encoded);if(encoded.size()>=196608)return error="config_too_large",false;
 nvs_handle_t h;if(nvs_open("matrix-bridge",NVS_READWRITE,&h)!=ESP_OK){error="storage_error";return error="storage_error",false;}bool okay=nvs_set_blob(h,"config",encoded.data(),encoded.size())==ESP_OK&&nvs_commit(h)==ESP_OK;nvs_close(h);if(!okay){error="storage_error";return error="storage_error",false;}
 deserializeJson(config,encoded);const char *zone=config["timezone"]|"";setenv("TZ",*zone?zone:"CST-8",1);tzset();++revision;error.clear();return true;
}
bool configSave(JsonVariantConst input,std::string &error){
 std::lock_guard<std::recursive_mutex> lock(guard);error="invalid_config";if(!input.is<JsonObjectConst>()||!input["printers"].is<JsonArrayConst>()||!input["accounts"].is<JsonArrayConst>()||!input["wifi"].is<JsonObjectConst>()||!input["pixel"].is<JsonObjectConst>()||input["printers"].size()>100||input["accounts"].size()>50)return error="invalid_config",false;
 DynamicJsonDocument next(configLimit);next["project"]="3dprint_pixelmonitor";next["schema"]=1;next["activePrinter"]=input["activePrinter"]|0u;next["timezone"]=input["timezone"]|(config["timezone"]|"");next["timezoneName"]=input["timezoneName"]|(config["timezoneName"]|"");next["language"]=input["language"]|(config["language"]|"");
 if(input.containsKey("language")&&!input["language"].is<const char*>())return error="invalid_preferences",false;
 const char *language=next["language"]|"";bool supported=!*language;for(const char *known:{"zh","zh-TW","en","fr","de","ru","ja","ko"})if(!strcmp(language,known))supported=true;
 if(!supported)return error="invalid_preferences",false;
 if(input.containsKey("onlineOTA")&&!input["onlineOTA"].is<bool>())return error="invalid_update_options",false;
 next["onlineOTA"]=input.containsKey("onlineOTA")?input["onlineOTA"].as<bool>():config["onlineOTA"]|true;
 auto display=next.createNestedObject("boardDisplay");
 for(const char *key:{"brightness","idleMinutes"}){
  auto value=input["boardDisplay"][key];
  const int brightness=!strcmp(key,"brightness");
  const int minimum=brightness?1:5, maximum=brightness?100:30;
  const int setting=value.isNull()?(config["boardDisplay"][key]|(brightness?80:10)):(value|0);
  if((!value.isNull()&&!value.is<int>())||setting<minimum||setting>maximum)
   return error="invalid_config",false;
  display[key]=setting;
 }
 if(!field(next["timezoneName"],96)||!field(next["timezone"],64)||next["activePrinter"].as<unsigned>()>=(input["printers"].size()?input["printers"].size():1))return error="invalid_selection",false;
 auto accounts=next.createNestedArray("accounts");for(JsonVariantConst a:input["accounts"].as<JsonArrayConst>()){
  const char *id=a["id"]|"",*region=a["region"]|"cn",*user=a["userId"]|"";if(!nameValid(id)||(!strcmp(region,"cn")?false:strcmp(region,"global")!=0)||!nameValid(user))return error="invalid_account",false;
  for(JsonObjectConst v:accounts)if(!strcmp(v["id"]|"",id))return error="duplicate_account",false;
  auto v=accounts.createNestedObject();v["id"]=id;v["name"]=a["name"]|id;v["region"]=region;v["userId"]=user;v["account"]=a["account"]|"";v["loginMethod"]=a["loginMethod"]|"token";v["loginCleared"]=a["loginCleared"]|false;if(!field(v["account"],128)||(strcmp(v["loginMethod"]|"","token")&&strcmp(v["loginMethod"]|"","password")))return error="invalid_account",false;
  JsonVariantConst oldAccount=previous(config["accounts"],id);if(strcmp(oldAccount["region"]|"cn",region)||strcmp(oldAccount["userId"]|"",user))oldAccount={};
  if(!field(v["name"],64)||!mergeSecret(v,a,oldAccount,"token",std::string("account:")+id,2048))return error="invalid_account_token",false;
  if(!v["loginCleared"].as<bool>()&&!v.containsKey("tokenEnc")&&!v.containsKey("token"))return error="missing_account_token",false;
  std::string loginBinding=std::string(id)+":"+region+":"+(v["account"]|"");JsonVariantConst oldLogin=oldAccount;if(strcmp(oldAccount["account"]|"",v["account"]|""))oldLogin={};
  if(a["passwordEnc"].is<const char*>()&&strlen(a["passwordEnc"]|"")){String plain;if(!xtouch_vault_open(a["passwordEnc"],loginBinding.c_str(),plain)||plain.length()>128)return error="invalid_account_password",false;v["passwordEncrypted"]=a["passwordEncrypted"]|true;if(v["passwordEncrypted"].as<bool>())v["passwordEnc"]=a["passwordEnc"];else v["password"]=plain;for(size_t i=0;i<plain.length();++i)plain.setCharAt(i,0);
  }else if(!mergeSecret(v,a,oldLogin,"password",loginBinding,128))return error="invalid_account_password",false;
  if(v["loginCleared"].as<bool>()){v.remove("token");v.remove("tokenEnc");v.remove("password");v.remove("passwordEnc");}
 }
 auto printers=next.createNestedArray("printers");for(JsonVariantConst p:input["printers"].as<JsonArrayConst>()){
  const char *id=p["id"]|"",*serial=p["serialNumber"]|"",*mode=p["mode"]|"lan";if(!nameValid(id)||!nameValid(serial)||(!strcmp(mode,"lan")?false:strcmp(mode,"cloud")!=0))return error="invalid_printer",false;
  for(JsonObjectConst old:printers)if(!strcmp(old["id"]|"",id))return error="duplicate_printer",false;
  auto v=printers.createNestedObject();for(const char *k:{"id","name","serialNumber","model","mode","host","accountId"})if(p[k].is<const char*>())v[k]=p[k];
  v["port"]=p["port"]|8883u;if(!v["port"].as<unsigned>()||v["port"].as<unsigned>()>65535||!field(v["name"],64)||!field(v["model"],64))return error="invalid_printer_fields",false;
  if(!strcmp(mode,"lan")){const char *host=v["host"]|"";if(!*host||strlen(host)>253)return error="invalid_host",false;for(const char *c=host;*c;c++)if(!isalnum((unsigned char)*c)&&*c!='.'&&*c!='-')return error="invalid_host",false;
   JsonVariantConst oldPrinter=previous(config["printers"],id);if(strcmp(oldPrinter["host"]|"",host)||strcmp(oldPrinter["serialNumber"]|"",serial))oldPrinter={};
   if(!mergeSecret(v,p,oldPrinter,"accessCode",std::string("printer:")+id,64)||(!v.containsKey("accessCodeEnc")&&!v.containsKey("accessCode")))return error="invalid_access_code",false;
  }else{bool found=false;for(JsonObjectConst a:accounts)if(!strcmp(a["id"]|"",v["accountId"]|""))found=true;if(!found)return error="missing_account",false;}
 }
 auto w=next.createNestedObject("wifi");JsonVariantConst incomingWifi=input["wifi"];w["ssid"]=incomingWifi["ssid"]|"";JsonVariantConst previousWifi=config["wifi"];if(strcmp(w["ssid"]|"",previousWifi["ssid"]|""))previousWifi={};if(!field(w["ssid"],32)||!mergeSecret(w,incomingWifi,previousWifi,"password","wifi",63))return error="invalid_wifi",false;
 auto wifiPassword=secret(w,"password","wifi");if(!wifiPassword.empty()&&wifiPassword.size()<8)return error="invalid_wifi_password",false;
 auto p=next.createNestedObject("pixel");JsonVariantConst source=input["pixel"];p["enabled"]=source["enabled"]|false;p["mac"]=source["mac"]|"";p["brightness"]=source["brightness"]|80u;p["screenOn"]=source["screenOn"]|true;p["rotate180"]=source["rotate180"]|false;if(source.containsKey("layout")&&!source["layout"].is<const char*>())return error="invalid_pixel_options",false;p["layout"]=source["layout"]|"printer";p["idleDisplay"]=source["idleDisplay"]|"clock";for(const char *key:{"resetOnConnect","clearOnConnect"}){if(source.containsKey(key)&&!source[key].is<bool>())return error="invalid_pixel_options",false;p[key]=source[key]|(config["pixel"][key]|false);}
 const char *mac=p["mac"];bool macOkay=strlen(mac)==17;for(size_t i=0;macOkay&&i<17;i++)macOkay=i%3==2?mac[i]==':':isxdigit((unsigned char)mac[i]);if((p["enabled"].as<bool>()&&!macOkay)||(*mac&&!macOkay))return error="invalid_pixel_mac",false;
 std::string canonical=mac;std::transform(canonical.begin(),canonical.end(),canonical.begin(),[](unsigned char c){return toupper(c);});p["mac"]=canonical;
 if(p["brightness"].as<unsigned>()<5||p["brightness"].as<unsigned>()>100||!pixel_screen::idleDisplayValid(p["idleDisplay"]|"")||!pixel_screen::printLayoutValid(p["layout"]|""))return error="invalid_pixel_options",false;
 const char *password=source["password"]|"";if(*password&&(strlen(password)!=6||strspn(password,"0123456789")!=6))return error="invalid_pixel_password",false;
 JsonVariantConst oldPixel=config["pixel"];if(strcmp(oldPixel["mac"]|"",canonical.c_str()))oldPixel={};
 if(!mergeSecret(p,source,oldPixel,"password",std::string("pixel:")+canonical,6))return error="invalid_pixel_password",false;
 return persistConfig(next,error);
}
void preferencesPublic(JsonObject out){
 std::lock_guard<std::recursive_mutex> lock(guard);out["language"]=config["language"]|"";out["timezone"]=config["timezone"]|"";out["timezoneName"]=config["timezoneName"]|"";
}
bool preferencesSave(JsonVariantConst input,std::string &error){
 if(!input.is<JsonObjectConst>())return error="invalid_preferences",false;
 std::lock_guard<std::recursive_mutex> lock(guard);DynamicJsonDocument next(configLimit);next.set(config);
 bool initial=input["initialize"]|false;
 bool zoneEmpty=!strlen(config["timezone"]|"");
 for(const char *key:{"language","timezone","timezoneName"})if(input.containsKey(key)){
  if(!input[key].is<const char*>())return error="invalid_preferences",false;
  if(initial&&!strcmp(key,"timezoneName")){if(zoneEmpty)next[key]=input[key];continue;}
  if(!initial||!strlen(config[key]|""))next[key]=input[key];
 }
 return configSave(next,error);
}
bool provisionWifi(JsonVariantConst input,std::string &error){
 if(!field(input["ssid"],32)||!strlen(input["ssid"]|"")||!field(input["password"],63))
  return error="invalid_wifi",false;
 const char *password=input["password"];
 if(*password&&strlen(password)<8)return error="invalid_wifi_password",false;
 std::lock_guard<std::recursive_mutex> lock(guard);
 DynamicJsonDocument next(configLimit);
 next.set(config);
 auto wifi=next["wifi"].to<JsonObject>();
 wifi["ssid"]=input["ssid"];
 if(*password&&!mergeSecret(wifi,input,{},"password","wifi",63))
  return error="storage_error",false;
 if(!persistConfig(next,error))return false;
 networkRetry();
 return true;
}

bool configReset(bool all){
 std::lock_guard<std::recursive_mutex> lock(guard);
 if(all)return nvs_flash_erase()==ESP_OK;
 DynamicJsonDocument next(configLimit);next.set(config);auto wifi=next["wifi"].to<JsonObject>();wifi["ssid"]="";wifi["clearPassword"]=true;
 std::string reason;return configSave(next.as<JsonVariantConst>(),reason);
}
bool configRestore(JsonVariantConst input,std::string &error){
 error="invalid_backup";const char *backupProject=input["project"]|"";if((strcmp(backupProject,"3dprint_pixelmonitor")&&strcmp(backupProject,"idotmatrix-bambulab"))||input["schema"]!=1)return false;DynamicJsonDocument plain(configLimit);plain.set(input);
 auto decode=[&](JsonObject node,const char *key,const std::string &binding){std::string enc=std::string(key)+"Enc";if(node[key].is<const char*>()&&node[std::string(key)+"Encrypted"]==false)return true;if(!node.containsKey(enc.c_str())){if(!strcmp(key,"password"))node["clearPassword"]=true;return !strcmp(key,"password");}String value;if(!xtouch_vault_open(node[enc.c_str()]|"",binding.c_str(),value))return false;node[key]=value;node.remove(enc.c_str());return true;};
 if(!decode(plain["wifi"],"password","wifi")||!decode(plain["pixel"],"password",std::string("pixel:")+(plain["pixel"]["mac"]|"")))return false;
 for(JsonObject a:plain["accounts"].as<JsonArray>()){if(!a["loginCleared"].as<bool>()&&!decode(a,"token",std::string("account:")+(a["id"]|"")))return false;if(!decode(a,"password",std::string(a["id"]|"")+":"+(a["region"]|"cn")+":"+(a["account"]|"")))return false;}
 for(JsonObject p:plain["printers"].as<JsonArray>())if(!strcmp(p["mode"]|"lan","lan")&&!decode(p,"accessCode",std::string("printer:")+(p["id"]|"")))return false;
 return configSave(plain.as<JsonVariantConst>(),error);
}
}
