#include "app.h"
#include "board.h"
#include "firmware_writer.h"
#include "update_policy.h"
#include <esp_app_desc.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <ctime>
#include <memory>

namespace bridge {
namespace {
constexpr const char *repository="https://github.com/tbc0309/esp-mosaico-vibe";
constexpr const char *releaseApi="https://api.github.com/repos/tbc0309/esp-mosaico-vibe/releases/latest";
struct Candidate{std::string version,url,sha;size_t size=0;};
std::mutex updateGuard;
Candidate candidate;
bool running=false;
unsigned progress=0;
std::string state="waiting",lastError,latest;
uint32_t nextCheck=20000;
uint32_t tick(){return uint32_t(esp_timer_get_time()/1000);}
bool enabled(){std::lock_guard<std::recursive_mutex> lock(guard);return config["onlineOTA"]|true;}
struct HttpDelete{void operator()(esp_http_client *client)const{
    void *context=nullptr;esp_http_client_get_user_data(client,&context);
    esp_http_client_close(client);esp_http_client_cleanup(client);delete static_cast<std::string*>(context);
}};
using Http=std::unique_ptr<esp_http_client,HttpDelete>;
Http open(const std::string &initial,int &status){
    std::string url=initial;
    for(unsigned redirects=0;redirects<5;++redirects){
        if(!enabled())return {};
        auto context=std::unique_ptr<std::string>(new(std::nothrow) std::string);if(!context)return {};
        esp_http_client_config_t options{};options.url=url.c_str();options.crt_bundle_attach=esp_crt_bundle_attach;
        options.timeout_ms=15000;options.buffer_size=4096;options.buffer_size_tx=1024;options.disable_auto_redirect=true;options.user_data=context.get();
        options.event_handler=[](esp_http_client_event_t *event)->esp_err_t{
            if(event->event_id==HTTP_EVENT_ON_HEADER&&event->header_key&&event->header_value&&!strcasecmp(event->header_key,"Location")&&strlen(event->header_value)<=4096)
                *static_cast<std::string*>(event->user_data)=event->header_value;
            return ESP_OK;
        };
        Http client(esp_http_client_init(&options));if(!client)return {};
        std::string *location=context.release();
        esp_http_client_set_header(client.get(),"User-Agent","3dprint_pixelmonitor");
        esp_http_client_set_header(client.get(),"Accept","application/vnd.github+json");
        if(esp_http_client_open(client.get(),0)!=ESP_OK)return {};
        esp_http_client_fetch_headers(client.get());status=esp_http_client_get_status_code(client.get());
        if(status!=301&&status!=302&&status!=303&&status!=307&&status!=308)return client;
        if(!update_policy::redirect(*location))return {};
        url=*location;
    }
    return {};
}
bool getJson(const std::string &url,DynamicJsonDocument &doc,size_t limit,int &status){
    auto client=open(url,status);if(!client||status!=200)return false;
    auto buffer=std::unique_ptr<char[]>(new(std::nothrow) char[4096]);if(!buffer)return false;
    std::string body;
    for(;;){
        if(!enabled())return false;
        int count=esp_http_client_read(client.get(),buffer.get(),4096);
        if(count<0)return false;if(!count)break;
        if(size_t(count)>limit-body.size())return false;
        body.append(buffer.get(),count);
    }
    return esp_http_client_is_complete_data_received(client.get())&&!deserializeJson(doc,body.c_str());
}
bool asset(JsonArrayConst assets,const std::string &filename,const std::string &url,JsonVariantConst &found){
    unsigned count=0;
    for(JsonVariantConst a:assets)if(filename==(a["name"]|"")){found=a;++count;}
    return count==1&&url==(found["browser_download_url"]|"");
}
bool checkRelease(Candidate &out,std::string &phase,std::string &error){
    DynamicJsonDocument release(65536);int status=0;
    if(!getJson(releaseApi,release,65536,status)){
        if(status==404){phase="unpublished";return true;}
        error=status==403||status==429?"update_rate_limited":"update_connection_failed";return false;
    }
    std::string version=release["tag_name"]|"";std::array<unsigned,3> parsed{};
    if(release["draft"]!=false||release["prerelease"]!=false||!update_policy::version(version,parsed)){error="invalid_release";return false;}
    out.version=version;
    if(!update_policy::newer(version,esp_app_get_description()->version)){phase="up_to_date";return true;}
    std::string prefix=std::string(repository)+"/releases/download/"+version+"/";
    std::string filename=std::string(board::projectName)+".bin";
    JsonVariantConst imageAsset;auto assets=release["assets"].as<JsonArrayConst>();
    if(!asset(assets,filename,prefix+filename,imageAsset)){error="incomplete_release";return false;}
    size_t assetSize=imageAsset["size"]|0u;std::string digest=imageAsset["digest"]|"";
    if(!assetSize||assetSize>board::flashBytes){error="invalid_release";return false;}
    release.clear();release.shrinkToFit();
    DynamicJsonDocument manifest(16384);status=0;
    std::string metadata="https://raw.githubusercontent.com/tbc0309/esp-mosaico-vibe/"+version+"/projects/3dprint_pixelmonitor/boards/"+board::id+"/ota.json";
    if(!getJson(metadata,manifest,16384,status)){error="update_connection_failed";return false;}
    auto *slot=esp_ota_get_next_update_partition(nullptr);
    if(!slot||strcmp(manifest["project"]|"","3dprint_pixelmonitor")||version!=(manifest["version"]|"")||
       strcmp(manifest["board"]|"",board::id)||strcmp(manifest["target"]|"",board::target)||
       strcmp(manifest["projectName"]|"",board::projectName)||size_t(manifest["flashBytes"]|0u)!=board::flashBytes||
       size_t(manifest["appSlotBytes"]|0u)!=slot->size){error="wrong_board_image";return false;}
    if(prefix+filename!=(manifest["url"]|"")){error="invalid_release";return false;}
    out.size=manifest["size"]|0u;out.sha=manifest["sha256"]|"";
    if(out.size!=assetSize||out.size>slot->size||!update_policy::checksum(out.sha)||
       (!digest.empty()&&digest!="sha256:"+out.sha)){error="invalid_release";return false;}
    out.url=prefix+filename;phase="available";return true;
}
bool install(const Candidate &image,std::string &error){
    int status=0;auto client=open(image.url,status);
    if(!client||status!=200){error="update_connection_failed";return false;}
    int64_t length=esp_http_client_get_content_length(client.get());
    if(length>0&&size_t(length)!=image.size){error="invalid_image";return false;}
    FirmwareWriter writer(image.size,image.sha,image.version);
    if(!writer.ready()){error=writer.error();return false;}
    auto buffer=std::unique_ptr<uint8_t[]>(new(std::nothrow) uint8_t[4096]);if(!buffer){error="no_memory";return false;}
    size_t received=0;
    while(received<image.size){
        if(!enabled()){error="update_disabled";return false;}
        int count=esp_http_client_read(client.get(),reinterpret_cast<char*>(buffer.get()),std::min(size_t(4096),image.size-received));
        if(count<=0){error="upload_interrupted";return false;}
        if(!writer.write(buffer.get(),count)){error=writer.error();return false;}
        received+=size_t(count);std::lock_guard<std::mutex> lock(updateGuard);progress=unsigned(received*100/image.size);
    }
    if(!esp_http_client_is_complete_data_received(client.get())){
        uint8_t extra=0;
        if(esp_http_client_read(client.get(),reinterpret_cast<char*>(&extra),1)!=0||!esp_http_client_is_complete_data_received(client.get())){error="invalid_image";return false;}
    }
    std::lock_guard<std::recursive_mutex> lock(guard);
    if(!(config["onlineOTA"]|true)){error="update_disabled";return false;}
    if(!writer.finish()){error=writer.error();return false;}
    return true;
}
void worker(void *argument){
    bool restart=false;
    {
        bool installing=argument!=nullptr;Candidate image;{std::lock_guard<std::mutex> lock(updateGuard);image=candidate;}
        std::string phase=installing?"installing":"checking",error;
        bool okay=installing?install(image,error):checkRelease(image,phase,error);
        bool active=enabled();
        {
            std::lock_guard<std::mutex> lock(updateGuard);running=false;lastError=error;
            if(!active){state="disabled";candidate={};lastError.clear();}
            else if(installing){state=okay?"restarting":"failed";restart=okay;}
            else{latest=image.version;state=okay?phase:"failed";candidate=okay&&phase=="available"?image:Candidate{};}
            nextCheck=tick()+(okay?6*60*60*1000u:10*60*1000u);
        }
    }
    if(restart){vTaskDelay(pdMS_TO_TICKS(1200));esp_restart();}
    vTaskDelete(nullptr);
}
bool start(bool installing){
    running=true;progress=0;lastError.clear();state=installing?"installing":"checking";
    if(xTaskCreate(worker,"online-update",12288,installing?reinterpret_cast<void*>(1):nullptr,1,nullptr)==pdPASS)return true;
    running=false;state="failed";lastError="no_memory";return false;
}
}
void updatePoll(){
    bool active=enabled();std::lock_guard<std::mutex> lock(updateGuard);
    if(!active){if(!running){state="disabled";candidate={};lastError.clear();}return;}
    if(state=="disabled"){state="waiting";nextCheck=tick()+1000;}
    if(!running&&wifiOnline&&time(nullptr)>1577836800&&int32_t(tick()-nextCheck)>=0)start(false);
}
bool updateRequest(bool installing,const std::string &version,std::string &error){
    if(!enabled()){error="update_disabled";return false;}
    if(!wifiOnline||time(nullptr)<1577836800){error="update_not_ready";return false;}
    std::lock_guard<std::mutex> lock(updateGuard);
    if(running||state=="restarting"){error="ota_busy";return false;}
    if(installing&&(state!="available"||candidate.version!=version||candidate.url.empty())){error="update_not_available";return false;}
    if(!start(installing)){error="no_memory";return false;}
    return true;
}
void updateStatus(JsonObject out){
    out["enabled"]=enabled();std::lock_guard<std::mutex> lock(updateGuard);
    out["state"]=state;out["busy"]=running;out["available"]=state=="available";out["current"]=esp_app_get_description()->version;
    out["latest"]=latest;out["progress"]=progress;out["error"]=lastError;
}
}
