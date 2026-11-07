template<class Setter> void clientHeaders(Setter set){
 set("User-Agent","bambu_network_agent/01.09.05.01");
 set("X-BBL-Client-Name","OrcaSlicer");
 set("X-BBL-Client-Type","slicer");
 set("X-BBL-Client-Version","01.09.05.51");
 set("X-BBL-Language","en-US");
 set("X-BBL-OS-Type","linux");
 set("X-BBL-OS-Version","6.2.0");
 set("X-BBL-Agent-Version","01.09.05.01");
 set("X-BBL-Executable-info","{}");
 set("X-BBL-Agent-OS-Type","linux");
 set("Content-Type","application/json");
}
using LimitedBody=XTouchLimitedBody<16384>;
bool uidFromToken(const String &token,String &uid) {
 int a=token.indexOf('.'),b=token.indexOf('.',a+1);if(a<1||b<=a+1||b-a>4096)return false;
 String payload=token.substring(a+1,b);payload.replace('-','+');payload.replace('_','/');while(payload.length()%4)payload+='=';
 size_t n=0;mbedtls_base64_decode(nullptr,0,&n,(const unsigned char *)payload.c_str(),payload.length());
 if(!n||n>4096)return false;auto decoded=std::unique_ptr<unsigned char[]>(new(std::nothrow) unsigned char[n+1]);if(!decoded)return false;
 if(mbedtls_base64_decode(decoded.get(),n,&n,(const unsigned char *)payload.c_str(),payload.length()))return false;decoded[n]=0;
 DynamicJsonDocument doc(6144);if(deserializeJson(doc,decoded.get(),n))return false;
 uid=doc["uid"].as<String>();if(uid=="null"||!uid.length())uid=doc["sub"].as<String>();
 if(uid=="null"||!uid.length())uid=doc["user_id"].as<String>();
 if(uid=="null"||!uid.length()||uid.length()>40)return false;
 if(!uid.startsWith("u_"))uid="u_"+uid;
 return true;
}
String webCsrf,webRegion;
uint32_t authBlockedAt[4]={};
bool authBlocked[4]={};
unsigned authRetryAfter=0;
int challengeStatus=0,responseStatus=0,lastReadError=0;
unsigned lastBodyBytes=0;
int request(const String &url,const String &token,const String &payload,String &reply) {
 reply="";responseStatus=0;lastReadError=0;lastBodyBytes=0;challengeStatus=0;authRetryAfter=0;
 if(time(nullptr)<1577836800)return -10;
 bool website=url.startsWith("https://bambulab.cn/")||url.startsWith("https://bambulab.com/");
 String site=url.startsWith("https://bambulab.cn/")?"https://bambulab.cn":"https://bambulab.com";
 if(website&&webRegion!=site){webCsrf="";webRegion=site;}
 const bool auth=token.isEmpty();
 const unsigned origin=(url.startsWith("https://bambulab.cn/")?0:url.startsWith("https://bambulab.com/")?1:url.startsWith("https://api.bambulab.cn/")?2:3);
 authRetryAfter=0;
 if(auth&&authBlocked[origin]){
  uint32_t elapsed=millis()-authBlockedAt[origin];
  if(elapsed<300000){authRetryAfter=(300000-elapsed+999)/1000;challengeStatus=0;return -12;}
  authBlocked[origin]=false;
 }
 if(website&&payload.length()){
  wipe(webCsrf);String unused;int csrfCode=request(site+"/api/csrf","","",unused);
  if(csrfCode<200||csrfCode>=300)return csrfCode;
  if(webCsrf.isEmpty())return -13;
 }
 struct Response {
  LimitedBody body;XTouchCloudCookies::Headers cookies;String mitigated,ray;
 } response;
 int status=0,code=-1;
 esp_http_client_config_t config={};config.url=url.c_str();config.crt_bundle_attach=esp_crt_bundle_attach;
 config.timeout_ms=12000;config.keep_alive_enable=false;config.buffer_size=2048;config.buffer_size_tx=1024;
 config.method=payload.length()?HTTP_METHOD_POST:HTTP_METHOD_GET;config.user_data=&response;
 config.event_handler=[](esp_http_client_event_t *event)->esp_err_t {
  auto *r=static_cast<Response *>(event->user_data);
  if(event->event_id==HTTP_EVENT_ON_DATA && event->data_len>0){
   if(r->body.write(static_cast<const uint8_t *>(event->data),event->data_len)!=size_t(event->data_len))return ESP_FAIL;
   if(r->body.overflow)return ESP_FAIL;
  }else if(event->event_id==HTTP_EVENT_ON_HEADER && event->header_key && event->header_value){
   if(!strcasecmp(event->header_key,"Set-Cookie"))r->cookies.add(event->header_value);
   else if(!strcasecmp(event->header_key,"cf-mitigated"))r->mitigated=event->header_value;
   else if(!strcasecmp(event->header_key,"cf-ray"))r->ray=event->header_value;
  }
  return ESP_OK;
 };
 esp_http_client_handle_t http=esp_http_client_init(&config);if(!http)return -1;
 esp_http_client_set_header(http,"Accept","application/json");
 if(website&&webCsrf.length()){
  String cookie="bbl_csrf_token="+webCsrf;esp_http_client_set_header(http,"Cookie",cookie.c_str());
  if(payload.length())esp_http_client_set_header(http,"x-bbl-csrf-token",webCsrf.c_str());
 }
 clientHeaders([&](const char *key,const char *value){esp_http_client_set_header(http,key,value);});
 if(token.length()){String bearer="Bearer "+token;esp_http_client_set_header(http,"Authorization",bearer.c_str());}
 if(payload.length())esp_http_client_set_post_field(http,payload.c_str(),payload.length());
 esp_err_t error=esp_http_client_perform(http);status=esp_http_client_get_status_code(http);
 // IDF's body dispatcher does not propagate an event handler's return value.
 if(response.body.failed&&!response.body.overflow&&error==ESP_OK)error=ESP_ERR_NO_MEM;
 responseStatus=status;lastReadError=error==ESP_OK?0:-int(error);
 reply=std::move(response.body.body);lastBodyBytes=reply.length();
 code=response.body.overflow?-11:error==ESP_OK?status:status>0?-14:-1;
 esp_http_client_cleanup(http);
 if(website&&url.endsWith("/api/csrf")&&!response.cookies.csrf.empty())webCsrf=response.cookies.csrf.c_str();
 /* The website TFA endpoint can deliver the session only as Set-Cookie.
    Use it solely after a successful response from that exact endpoint. */
 if(code==200&&website&&url.endsWith("/api/sign-in/tfa")&&!response.cookies.token.empty()){
  DynamicJsonDocument auth(6144);
  bool valid=reply.isEmpty()||!deserializeJson(auth,reply);
  if(valid&&(auth.isNull()||auth.is<JsonObject>())){
   if(auth.isNull())auth.to<JsonObject>();
   if(!(auth["accessToken"]|"")[0]&&!(auth["token"]|"")[0]&&!(auth["data"]["accessToken"]|"")[0]&&!(auth["data"]["token"]|"")[0]){
    auth["token"]=response.cookies.token.c_str();reply="";serializeJson(auth,reply);
   }
  }
 }
 if(xtouch_cloud_challenge(status,reply.c_str(),response.mitigated.c_str(),response.ray.c_str())){
  challengeStatus=status;
  if(auth){authBlocked[origin]=true;authBlockedAt[origin]=millis();authRetryAfter=300;}
  code=-12;
 }
 return code;
}
