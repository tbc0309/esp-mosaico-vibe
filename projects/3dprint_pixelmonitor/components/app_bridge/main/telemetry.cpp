#include "app.h"
#include "ported/features/pixel_screen/head_motion.h"
#include "ported/features/pixel_screen/chamber.h"
#include "ported/features/pixel_screen/filament.h"
#include "ported/features/network/print_stage_report.h"
#include "ported/features/network/print_job_state.h"
#include "ported/features/network/print_phase.h"
#include "ported/xtouch/ams_units.h"
#include "ported/xtouch/protocol_telemetry.h"
#include <algorithm>
#include <cmath>

BambuTelemetry bambuStatus{};
#include "ported/xtouch/ams_report.h"
namespace bridge {
namespace { std::string state="IDLE",identity;pixel_screen::Snapshot view;bool fault=false;pixel_screen::HeadMotion headMotion; }
std::string modelShort(const std::string &input){
 std::string s=input;for(auto &c:s)c=toupper((unsigned char)c);if(s.rfind("BAMBU LAB ",0)==0)s.erase(0,10);s.erase(std::remove(s.begin(),s.end(),' '),s.end());
 const std::pair<const char*,const char*> names[]={{"BL-P001","X1C"},{"3DPRINTER-X1-CARBON","X1C"},{"X1CARBON","X1C"},{"BL-P002","X1"},{"3DPRINTER-X1","X1"},{"C13","X1E"},{"C11","P1P"},{"C12","P1S"},{"N1","A1MINI"},{"N2S","A1"},{"N6","X2D"},{"N7","P2S"},{"N9","A2L"},{"N10","H3C"},{"O1D","H2D"},{"O1C","H2C"},{"O1C2","H2C"},{"O1S","H2S"},{"O1E","H2DPRO"},{"O2D","H2DPRO"}};
 for(auto &v:names)if(s==v.first)return v.second;if(s.empty())return "--";return s.substr(0,6);
}
void telemetryReset(const std::string &model){
 std::lock_guard<std::recursive_mutex> lock(guard);memset(&bambuStatus,0,sizeof(bambuStatus));bambuStatus.m_tray_now=bambuStatus.m_tray_tar=255;bambuStatus.print_stage_current=-1;memset(bambuStatus.ams_bound_extruder,255,sizeof(bambuStatus.ams_bound_extruder));state="IDLE";identity.clear();headMotion.reset();fault=false;view={};view.progressLayout=false;auto text=modelShort(model);snprintf(view.model,sizeof(view.model),"%s",text.c_str());
}
void telemetryReport(JsonObjectConst p){
 std::lock_guard<std::recursive_mutex> lock(guard);
 auto integer=[](JsonVariantConst v,int low,int high,int &out){if(v.is<bool>()||v.isNull())return false;double d;if(v.is<const char*>()){const char *s=v.as<const char*>();char *end;d=strtod(s,&end);if(!*s||*end)return false;}else if(v.is<double>())d=v.as<double>();else return false;if(!std::isfinite(d)||std::floor(d)!=d||d<low||d>high)return false;out=int(d);return true;};
 std::string next=p["gcode_state"]|state.c_str();std::string task=p["subtask_id"].is<const char*>()?p["subtask_id"].as<const char*>():p["gcode_file"]|identity.c_str();
 bool changed=!identity.empty()&&!task.empty()&&identity!=task;bool started=XTouchPrintJob::beginReport(bambuStatus,state.c_str(),next.c_str(),changed);
 if(started){headMotion.reset();bambuStatus.ams_current_valid=false;bambuStatus.ams_target_valid=false;bambuStatus.ams_status_valid=false;fault=false;}
 bool hadLayer=bambuStatus.current_layer_valid&&bambuStatus.current_layer>0;int n;
 if(p.containsKey("layer_num"))bambuStatus.current_layer_valid=integer(p["layer_num"],0,100000,bambuStatus.current_layer);
 if(integer(p["total_layer_num"],0,100000,n))bambuStatus.total_layers=n;
 if(integer(p["mc_percent"],0,100,n))bambuStatus.mc_print_percent=n;
 if(integer(p["mc_remaining_time"],0,1000000,n))bambuStatus.mc_left_time=n;
 if(integer(p["print_error"],0,INT32_MAX,n))fault=n!=0;
 auto temperature=[](JsonVariantConst v,int maximum,int &out){
  if(v.isNull()||v.is<bool>())return;double value;
  if(v.is<const char*>()){const char *text=v.as<const char*>();char *end;value=strtod(text,&end);if(!*text||*end)return;}
  else if(v.is<double>())value=v.as<double>();else return;
  if(std::isfinite(value)&&value>=0&&value<=maximum)out=int(std::lround(value));
 };
 temperature(p["nozzle_temper"],400,view.nozzle);temperature(p["bed_temper"],160,view.bed);
 auto bed=XTouchTelemetry::bed(p);if(bed.present)view.bed=bed.actualValid?int(bed.actual):-1;
 auto chamber=XTouchTelemetry::chamber(p);
 if(chamber.actualPresent){bambuStatus.chamber_valid=chamber.actualValid;if(chamber.actualValid)bambuStatus.chamber_temper=chamber.actual;}
 if(chamber.targetPresent){bambuStatus.chamber_target_valid=chamber.targetValid;if(chamber.targetValid)bambuStatus.chamber_target_temper=chamber.target;}
 if(p["support_chamber_temp_edit"].is<bool>()){bambuStatus.chamber_edit_known=true;bambuStatus.chamber_edit_supported=p["support_chamber_temp_edit"];}

 bambuStatus.print_stage_current=XTouchStageReport::update(p,bambuStatus.print_stage_current,started,!hadLayer&&bambuStatus.current_layer_valid&&bambuStatus.current_layer>0,XTouchPrintJob::terminal(next.c_str()));
 if(p.containsKey("ams_status")){bambuStatus.ams_status_valid=integer(p["ams_status"],0,65535,n);if(bambuStatus.ams_status_valid){bambuStatus.ams_status_main=n>>8;bambuStatus.ams_status_sub=n&255;}}
 xtouch_mqtt_parse_ams_report(p["ams"].as<JsonObjectConst>());
 auto material=[](JsonObjectConst source,unsigned index){
  if(!source.containsKey("tray_color"))return;const char *c=source["tray_color"]|"";bool valid=strlen(c)==8&&strspn(c,"0123456789abcdefABCDEF")==8;
  uint8_t bit=1U<<index;if(valid){bambuStatus.external_color_valid|=bit;uint32_t color=strtoul(c,nullptr,16)>>8;if(index)bambuStatus.external_color2=color;else bambuStatus.external_color=color;}else bambuStatus.external_color_valid&=~bit;
 };
 if(p["vt_tray"].is<JsonObjectConst>()){bambuStatus.external_present=true;bambuStatus.external_materials=1;material(p["vt_tray"],0);}
 if(p["vir_slot"].is<JsonArrayConst>()){
  bambuStatus.external_multi_protocol=true;bambuStatus.external_present=true;bambuStatus.external_materials=0;
  for(JsonObjectConst v:p["vir_slot"].as<JsonArrayConst>()){int id=v["id"]|-1;unsigned index=id==254?1:0;if(id!=255&&id!=254)continue;bambuStatus.external_materials|=1U<<index;material(v,index);}
 }
 JsonObjectConst heads=p["device"]["extruder"];if(integer(heads["state"],0,INT32_MAX,n)){unsigned count=n&15;if(count>=1&&count<=2){bambuStatus.nozzle_count=count;bambuStatus.active_nozzle=((n>>4)&15)<count?(n>>4)&15:255;}}
 for(JsonObjectConst head:heads["info"].as<JsonArrayConst>()){
  unsigned id=(head["id"]|0u)&15u;if(id>=2)continue;
  if(head["snow"].is<unsigned>()){unsigned packed=head["snow"],unit=packed>>8,slot=packed&255;int index=xtouch_ams_unit_index(unit);bambuStatus.nozzle_loaded_valid|=1U<<id;bambuStatus.nozzle_loaded[id]=packed==65535?255:index>=0&&slot<xtouch_ams_slot_count(index)?index*4+slot:unit==255?254:unit==254?(bambuStatus.nozzle_count==1&&!bambuStatus.external_multi_protocol?254:253):-1;}
  if(head["temp"].is<uint32_t>()&&id==bambuStatus.active_nozzle){unsigned temp=head["temp"];if((temp&65535)<=400)view.nozzle=temp&65535;}
 }
 if(p["printer_type"].is<const char*>()){auto m=modelShort(p["printer_type"]);if(m!="--")snprintf(view.model,sizeof(view.model),"%s",m.c_str());}
 state=next;if(!task.empty())identity=task;XTouchPrintJob::finishReport(bambuStatus,state.c_str());
 view.state=fault?pixel_screen::State::Error:state=="PREPARE"?pixel_screen::State::Preparing:state=="RUNNING"?(xtouch_print_preparing(false,true,bambuStatus.current_layer_valid,bambuStatus.current_layer)?pixel_screen::State::Preparing:pixel_screen::State::Printing):state=="PAUSE"?pixel_screen::State::Paused:state=="FINISH"?pixel_screen::State::Complete:state=="FAILED"?pixel_screen::State::Cancelled:pixel_screen::State::Idle;
 auto colors=pixel_screen::filamentColors(bambuStatus);view.filamentKnown=colors.known;view.filamentColor=colors.current;view.feedColor=colors.feed;
 view.percent=bambuStatus.mc_print_percent;view.currentLayer=bambuStatus.current_layer_valid?bambuStatus.current_layer:-1;view.totalLayers=bambuStatus.total_layers;view.leftMinutes=bambuStatus.mc_left_time;view.stage=bambuStatus.print_stage_current;view.amsMain=bambuStatus.ams_status_valid?bambuStatus.ams_status_main:-1;
 view.chamberHeating=pixel_screen::chamberWarming(bambuStatus);
 view.headParked=headMotion.observe(view,bambuStatus.ams_current_valid?bambuStatus.m_tray_now:-1,bambuStatus.ams_target_valid?bambuStatus.m_tray_tar:-1);
}
pixel_screen::Snapshot telemetrySnapshot(){std::lock_guard<std::recursive_mutex> lock(guard);return view;}
}
