'use strict';
window.MatrixTimezonePicker=(()=>{
 const data=MatrixWindowsZones;
 let baseline='',custom='',original=null;
 function label(zone){
  const language=MatrixI18n.language();
  return zone.labels[language]||zone.labels.en||zone.id;
 }
 function render(value){
  const select=document.getElementById('timezone');select.replaceChildren();
  if(custom)select.append(new Option(MatrixI18n.text('当前自定义时区','Current custom timezone')+' · '+custom,'__custom__'));
  for(const zone of [...data.zones].sort((a,b)=>a.offset-b.offset||a.id.localeCompare(b.id)))select.append(new Option(label(zone),zone.id));
  select.value=value;
 }
 function fill(config){
  const name=data.aliases[config.timezoneName]||config.timezoneName;
  baseline=data.zones.some(zone=>zone.id===name)?name:'__custom__';
  if(!config.timezone&&!config.timezoneName)baseline='China Standard Time';
  custom=baseline==='__custom__'?(config.timezone||'CST-8'):'';
  original={timezone:config.timezone||'CST-8',timezoneName:config.timezoneName||''};render(baseline);
 }
 function changed(){return document.getElementById('timezone').value!==baseline;}
 function selection(){
  const name=document.getElementById('timezone').value;
  if(!changed())return original;
  return MatrixTimezones[name]?{timezone:MatrixTimezones[name],timezoneName:name}:null;
 }
 function refresh(){render(document.getElementById('timezone').value);}
 return {fill,selection,refresh,changed};
})();
