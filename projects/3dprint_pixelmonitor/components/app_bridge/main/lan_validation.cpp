#include "app.h"
#include <mqtt_client.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <algorithm>
#include <cctype>
// Native equivalent of xTouch-CN XTouchLanProbe: subscribe only, no publishes.
namespace bridge {
std::atomic<bool> printerProbeRequested{false},printerProbePaused{false};
namespace {
struct Probe {std::string topic;std::atomic<bool> received{false},rejected{false},connected{false};};
void probeEvent(void *arg,esp_event_base_t,int32_t id,void *data){auto &p=*static_cast<Probe*>(arg);auto *e=static_cast<esp_mqtt_event_handle_t>(data);if(id==MQTT_EVENT_CONNECTED){p.connected=true;esp_mqtt_client_subscribe(e->client,p.topic.c_str(),0);}if(id==MQTT_EVENT_DATA&&e->current_data_offset==0&&e->topic&&std::string(e->topic,e->topic_len)==p.topic)p.received=true;if(id==MQTT_EVENT_ERROR&&e->error_handle&&(e->error_handle->connect_return_code==4||e->error_handle->connect_return_code==5))p.rejected=true;}
}
void lanValidate(JsonVariantConst input,JsonObject result){
 std::string host=input["host"]|"",serial=input["serialNumber"]|"",password=input["accessCode"]|"";
 if(password.empty()){DynamicJsonDocument snapshot(configLimit);configCopy(snapshot);for(JsonVariantConst p:snapshot["printers"].as<JsonArrayConst>())if(!strcmp(p["mode"]|"lan","lan")&&!strcmp(p["id"]|"",input["id"]|"")&&host==(p["host"]|"")&&serial==(p["serialNumber"]|"")){password=secret(p,"accessCode",std::string("printer:")+(p["id"]|""));break;}}
 bool valid=!host.empty()&&host.size()<=253&&!serial.empty()&&serial.size()<=40&&password.size()==8;for(char c:host)valid&=isalnum((unsigned char)c)||c=='.'||c=='-';for(char c:serial)valid&=isalnum((unsigned char)c)||c=='_'||c=='-';for(unsigned char c:password)valid&=c>=33&&c<=126;
 if(!valid){result["error"]="invalid_printer_fields";return;}if(!wifiOnline){result["error"]="no_network";return;}
 printerProbeRequested=true;uint64_t started=esp_timer_get_time();while(!printerProbePaused&&esp_timer_get_time()-started<5000000)vTaskDelay(pdMS_TO_TICKS(20));
 if(!printerProbePaused){printerProbeRequested=false;result["error"]="busy";return;}
 Probe probe;probe.topic="device/"+serial+"/report";std::string uri="mqtts://"+host+":8883",clientId="IDM-VERIFY-"+std::to_string(esp_random());esp_mqtt_client_config_t c{};c.broker.address.uri=uri.c_str();c.broker.verification.skip_cert_common_name_check=true;c.credentials.client_id=clientId.c_str();c.credentials.username="bblp";c.credentials.authentication.password=password.c_str();c.session.keepalive=10;c.network.timeout_ms=10000;c.network.disable_auto_reconnect=true;c.task.stack_size=6144;c.buffer.size=2048;
 auto client=esp_mqtt_client_init(&c);if(client){esp_mqtt_client_register_event(client,MQTT_EVENT_ANY,probeEvent,&probe);if(esp_mqtt_client_start(client)==ESP_OK){started=esp_timer_get_time();while(!probe.received&&!probe.rejected&&wifiOnline&&esp_timer_get_time()-started<20000000)vTaskDelay(pdMS_TO_TICKS(25));}esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);}
 std::fill(password.begin(),password.end(),0);printerProbeRequested=false;result["ok"]=probe.received.load();if(!probe.received)result["error"]=probe.rejected?"lan_auth_rejected":probe.connected?"lan_status_timeout":"lan_connection_failed";
}
}
