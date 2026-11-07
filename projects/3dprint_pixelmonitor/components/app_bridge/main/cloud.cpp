#include "app.h"
#include "ported/platform/idf/runtime.h"
#include "ported/xtouch/cloud_login.h"
#include "ported/xtouch/cloud_ca.h"
#include "ported/xtouch/cloud_cookies.h"
#include "ported/xtouch/cloud_response.h"
#include "ported/features/network/limited_body.h"
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <mbedtls/base64.h>
#include <memory>
#include <ctime>
#include <map>

namespace {
void wipe(String &s){for(size_t i=0;i<s.length();i++)s.setCharAt(i,0);s="";}
#include "cloud_request.inc"
std::mutex jobGuard;
String inputText,resultText;
bool running=false,complete=false;
std::map<std::string,uint32_t> codeUntil;std::string challengeAccount;uint32_t challengeUntil=0;
uint32_t finishedAt=0;
void worker(void*){
 { // Release every JSON/String/TLS owner before deleting the FreeRTOS task.
 DynamicJsonDocument input(8192);{std::lock_guard<std::mutex> lock(jobGuard);deserializeJson(input,inputText);wipe(inputText);}
 String mode=input["action"]|"",region=input["region"]|"cn",token=input["token"]|"",remembered,body,reply;DynamicJsonDocument output(0);int code=-1;
 if(input["accountId"].is<const char*>()&&token.isEmpty()){
  DynamicJsonDocument snapshot(bridge::configLimit);bridge::configCopy(snapshot);for(JsonVariantConst a:snapshot["accounts"].as<JsonArrayConst>())if(!strcmp(a["id"]|"",input["accountId"]|"")){token=bridge::secret(a,"token",std::string("account:")+(a["id"]|"")).c_str();region=a["region"]|"cn";if(mode=="login"&&!* (input["password"]|"")&&!strcmp(input["account"]|(a["account"]|""),a["account"]|""))input["password"]=bridge::secret(a,"password",std::string(a["id"]|"")+":"+(a["region"]|"cn")+":"+(a["account"]|""));if(!input["account"].is<const char*>())input["account"]=a["account"]|"";break;}
 }
 std::string requestAccount=input["account"]|"",requestId=input["accountId"]|"";
 if(mode=="validate_lan"){XTouchCloudMemory::output(output,512);bridge::lanValidate(input,output.to<JsonObject>());}
 else if(mode=="login"||mode=="login_code"||mode=="login_tfa"||mode=="send_code")code=XTouchCloudLogin::execute(input,output,mode,region,token,remembered,body,reply,request,uidFromToken);
 else if(mode=="devices"){
  auto *endpoint=XTouchCloudEndpoints::get(region.c_str());code=request(String(endpoint->website)+"/api/v1/iot-service/api/user/bind",token,"",reply);if(code==-1||code==-14||code==-12||code==404)code=request(String(endpoint->api)+"/v1/iot-service/api/user/bind",token,"",reply);
  XTouchCloudMemory::output(output,16384);DynamicJsonDocument data(24576);if(code==200&&!deserializeJson(data,reply)){auto devices=output.createNestedArray("devices");unsigned count=0;for(JsonObjectConst d:data["devices"].as<JsonArrayConst>()){if(count++==100)break;auto p=devices.createNestedObject();p["serialNumber"]=d["dev_id"];p["name"]=d["name"];p["model"]=d["dev_product_name"];p["online"]=d["online"]|false;}output["ok"]=true;}
 }else if(mode=="token"){
  // Validate against the same authenticated endpoint used to read bound printers.
  // JWT claims identify the account only after the server accepts its token.
  auto *endpoint=XTouchCloudEndpoints::get(region.c_str());String uid;bool valid=uidFromToken(token,uid);
  code=request(String(endpoint->website)+"/api/v1/iot-service/api/user/bind",token,"",reply);if(code==-1||code==-14||code==-12||code==404)code=request(String(endpoint->api)+"/v1/iot-service/api/user/bind",token,"",reply);
  DynamicJsonDocument data(24576);bool accepted=code==200&&!deserializeJson(data,reply)&&data["devices"].is<JsonArray>();data.clear();data.shrinkToFit();
  if(accepted&&!valid){code=request(String(endpoint->api)+"/v1/user-service/my/profile",token,"",reply);DynamicJsonDocument p(4096);if(code==200&&!deserializeJson(p,reply)){uid=p["uidStr"]|"";if(uid.isEmpty())uid=p["uid"].as<String>();valid=!uid.isEmpty()&&uid!="null";}}
  XTouchCloudMemory::output(output);if(accepted&&valid){output["ok"]=true;output["token"]=token;output["userId"]=uid.startsWith("u_")?uid:"u_"+uid;}
 }
 if(!output.capacity())XTouchCloudMemory::output(output);output["httpStatus"]=code;if(!output["ok"].as<bool>()&&!output.containsKey("error"))output["error"]=code==-10?"time_unavailable":code==-12?"browser_verification_required":code==401?"authentication_rejected":code==403?"cloud_forbidden":code==429?"cloud_rate_limited":"cloud_connection_failed";
 if(output.overflowed()){output.clear();output["ok"]=false;output["error"]="response_too_large";}
 if(remembered.length()&&((output["ok"]|false)||!strcmp(output["error"]|"","verification_required")||!strcmp(output["error"]|"","two_factor_required")))output["passwordEnc"]=remembered;
 if(mode!="send_code"&&mode!="validate_lan")bridge::cloudAccountResult(requestId.c_str(),output["ok"]|false,!strcmp(output["error"]|"","authentication_rejected"));
 {std::lock_guard<std::mutex> lock(jobGuard);if(!strcmp(output["error"]|"","verification_required")){challengeAccount=requestAccount;challengeUntil=millis()+120000;}else if((output["ok"]|false)&&(mode=="login"||mode=="login_code"||mode=="login_tfa")){challengeAccount.clear();challengeUntil=0;}wipe(resultText);serializeJson(output,resultText);finishedAt=millis();running=false;complete=true;}wipe(token);wipe(remembered);wipe(body);wipe(reply);
 }
 vTaskDelete(nullptr);
}
}
namespace bridge {
void cloudBegin(){}
bool cloudStart(JsonVariantConst input,bool internal){
 if(cloudBackgroundBusy()&&!internal)return false;
 const char *action=input["action"]|"",*region=input["region"]|"cn";if(strcmp(region,"cn")&&strcmp(region,"global"))return false;if(!strcmp(region,"global")&&!strcmp(action,"send_code"))return false;
 bool allowed=false;for(const char *a:{"login","login_code","login_tfa","send_code","devices","token","validate_lan"})if(!strcmp(action,a))allowed=true;if(!allowed||measureJson(input)>6144)return false;
 std::lock_guard<std::mutex> lock(jobGuard);if(running)return false;if(!strcmp(region,"global")&&!strcmp(action,"login_code")&&(challengeAccount!=(input["account"]|"")||int32_t(challengeUntil-millis())<=0))return false;std::string codeKey=std::string(region)+":"+(input["account"]|"");if(!strcmp(action,"send_code")){if(codeUntil.count(codeKey)&&int32_t(codeUntil[codeKey]-millis())>0)return false;if(codeUntil.size()>100)codeUntil.clear();codeUntil[codeKey]=millis()+60000;}wipe(inputText);serializeJson(input,inputText);wipe(resultText);complete=false;running=true;
 if(xTaskCreate(worker,"cloud-api",16384,nullptr,1,nullptr)!=pdPASS){running=false;wipe(inputText);return false;}return true;
}
void cloudStatus(JsonObject out){std::lock_guard<std::mutex> lock(jobGuard);if(complete&&uint32_t(millis()-finishedAt)>120000){wipe(resultText);complete=false;}out["running"]=running;out["complete"]=complete;if(complete){DynamicJsonDocument result(24576);if(!deserializeJson(result,resultText))out["result"]=result;}}
void cloudClear(){std::lock_guard<std::mutex> lock(jobGuard);if(!running){wipe(resultText);complete=false;}}
}
