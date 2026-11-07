#include "app.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace bridge {
namespace {
void worker(void*){
 std::string line;line.reserve(512);bool overflow=false;
 for(;;){uint8_t c;int n=uart_read_bytes(UART_NUM_0,&c,1,pdMS_TO_TICKS(100));if(n<=0)continue;if(c=='\r')continue;
  if(c!='\n'){if(line.size()<1024)line+=char(c);else overflow=true;continue;}
  if(!overflow&&line.rfind("wifi ",0)==0){DynamicJsonDocument input(1024);if(!deserializeJson(input,line.c_str()+5)&&input["ssid"].is<const char*>()&&input["password"].is<const char*>()){
    DynamicJsonDocument candidate(configLimit);configCopy(candidate);candidate["wifi"]=input;std::string error;bool okay=configSave(candidate.as<JsonVariantConst>(),error);ESP_LOGI("serial","Wi-Fi settings %s",okay?"saved":error.c_str());
   }else ESP_LOGW("serial","Invalid Wi-Fi command");
  }else if(!overflow&&line=="status"){DynamicJsonDocument d(8192);networkStatus(d.createNestedObject("network"));{std::lock_guard<std::recursive_mutex> lock(guard);d["configCapacity"]=config.capacity();d["configUsage"]=config.memoryUsage();d["revision"]=revision.load();d["ssidLength"]=strlen(config["wifi"]["ssid"]|"");}std::string t;serializeJson(d,t);ESP_LOGI("serial","%s",t.c_str());}else if(!overflow&&line=="setup"){setupMode(true);ESP_LOGI("serial","Setup hotspot enabled");}
  std::fill(line.begin(),line.end(),0);line.clear();overflow=false;
 }
}
}
void serialBegin(){if(uart_driver_install(UART_NUM_0,2048,0,0,nullptr,0)==ESP_OK)xTaskCreate(worker,"serial-setup",4096,nullptr,1,nullptr);}
}
