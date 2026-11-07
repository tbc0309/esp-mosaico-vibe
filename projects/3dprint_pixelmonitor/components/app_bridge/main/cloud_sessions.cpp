#include "app.h"
#include <esp_timer.h>
#include <ctime>
#include <map>
// xTouch-CN's startup/six-hour account sweep, adapted to the shared native worker.
namespace bridge {
namespace {
std::atomic<bool> background{false};std::mutex healthGuard;std::map<std::string,std::string> health;std::map<std::string,uint32_t> rejectedAt;std::string rejectedAccount;
uint32_t nextSweep=0,nextCheck=15000;size_t cursor=0;bool renewing=false;std::string accountId,expected;
uint32_t tick(){return uint32_t(esp_timer_get_time()/1000);}
std::string encode(JsonVariantConst account){std::string value;serializeJson(account,value);return value;}
void setHealth(const std::string &id,const char *state){std::lock_guard<std::mutex> lock(healthGuard);if(health.size()>100)health.clear();health[id]=state;}
}
void cloudAccountResult(const char *id,bool okay,bool rejected){if(!id||!*id)return;if(okay){setHealth(id,"valid");std::lock_guard<std::mutex> lock(healthGuard);rejectedAt.erase(id);}else if(rejected)setHealth(id,"invalid");}
void cloudAccountRejected(const char *id){if(!id||!*id)return;std::lock_guard<std::mutex> lock(healthGuard);auto now=tick();if(rejectedAt.count(id)&&uint32_t(now-rejectedAt[id])<21600000)return;if(rejectedAt.size()>100)rejectedAt.clear();rejectedAt[id]=now;rejectedAccount=id;}
bool cloudBackgroundBusy(){return background.load();}
void cloudAccountHealth(const char *id,JsonObject out){std::lock_guard<std::mutex> lock(healthGuard);auto i=health.find(id);if(i!=health.end())out["health"]=i->second;}
void cloudSessionsPoll(){
 uint32_t now=tick();if(int32_t(now-nextCheck)<0)return;nextCheck=now+1000;
 if(background){DynamicJsonDocument job(24576);cloudStatus(job.to<JsonObject>());if(job["running"]|false)return;
  DynamicJsonDocument snapshot(configLimit);configCopy(snapshot);JsonObject account;for(JsonObject a:snapshot["accounts"].as<JsonArray>())if(accountId==(a["id"]|"")){account=a;break;}
  auto result=job["result"].as<JsonVariantConst>();if(account.isNull()||encode(account)!=expected){cloudClear();background=false;++cursor;return;}
  if(!renewing&&!strcmp(result["error"]|"","authentication_rejected")&&!account["loginCleared"].as<bool>()&&strcmp(account["loginMethod"]|"token","token")&&(!secret(account,"password",accountId+":"+(account["region"]|"cn")+":"+(account["account"]|"")).empty())){
   StaticJsonDocument<512> request;request["action"]="login";request["accountId"]=accountId;request["region"]=account["region"];request["account"]=account["account"];cloudClear();renewing=true;{std::lock_guard<std::mutex> lock(healthGuard);rejectedAt[accountId]=now;}if(cloudStart(request,true))return;
  }
  if(renewing&&(result["ok"]|false)&&result["token"].is<const char*>()&&!strcmp(result["userId"]|"",account["userId"]|"")){
   account["token"]=result["token"];account["userId"]=result["userId"];std::string error;if(configSave(snapshot,error))setHealth(accountId,"valid");else setHealth(accountId,"unknown");
  }else if(result["ok"]|false)setHealth(accountId,"valid");
  else if(!strcmp(result["error"]|"","authentication_rejected")||!strcmp(result["error"]|"","verification_required")||!strcmp(result["error"]|"","two_factor_required"))setHealth(accountId,"invalid");else setHealth(accountId,"unknown");
  cloudClear();background=false;renewing=false;++cursor;nextCheck=now+2000;return;
 }
 if(!wifiOnline||time(nullptr)<1577836800)return;
 DynamicJsonDocument state(24576);cloudStatus(state.to<JsonObject>());if((state["running"]|false)||(state["complete"]|false))return;
 if(!nextSweep||int32_t(now-nextSweep)>=0){cursor=0;nextSweep=now+21600000;}
 DynamicJsonDocument snapshot(configLimit);configCopy(snapshot);auto accounts=snapshot["accounts"].as<JsonArrayConst>();std::string rejected;{std::lock_guard<std::mutex> lock(healthGuard);rejected.swap(rejectedAccount);}if(!rejected.empty()){for(size_t i=0;i<accounts.size();++i)if(rejected==(accounts[i]["id"]|"")){cursor=i;break;}}if(cursor>=accounts.size())return;auto a=accounts[cursor];
 if(a["loginCleared"]|false){++cursor;return;}accountId=a["id"]|"";expected=encode(a);renewing=false;
 StaticJsonDocument<512> request;request["action"]="devices";request["accountId"]=accountId;request["region"]=a["region"]|"cn";
 background=true;if(!cloudStart(request,true)){background=false;++cursor;}
}
}
