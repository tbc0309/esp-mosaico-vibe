'use strict';
window.MatrixUpdates=(()=>{
 let options,last={},busy=false,notified='',requested=null;
 const $=id=>document.getElementById(id),text=(zh,en)=>options.text(zh,en);
 function render(value){
  if(value)last=value;if(!options)return;
  const enabled=requested??(options.config()?.onlineOTA!==false),state=last.state||'waiting',working=busy||!!last.busy;
  $('online-ota').checked=enabled;$('online-ota').disabled=busy||state==='installing'||state==='restarting';
  const install=enabled&&(last.available||state==='installing'||state==='restarting'),button=$('update-check');
  button.disabled=!enabled||working||state==='restarting';button.dataset.action=install?'install':'check';button.classList.toggle('primary',!!install);
  button.textContent=install?text('立即更新','Update now'):text('检查更新','Check updates');
  const labels={waiting:text('联网后检查新版本','Check for updates after connecting'),disabled:text('在线 OTA 已关闭','Online OTA is off'),checking:text('正在检查更新…','Checking for updates…'),up_to_date:text('已是最新版本','Already up to date'),unpublished:text('暂无已发布版本','No published release yet'),available:text('发现新版本：','New version: ')+(last.latest||''),installing:text('正在升级…','Updating…')+' '+(last.progress||0)+'%',restarting:text('升级完成，正在重启…','Update complete; restarting…'),failed:text('检查或升级失败，请重试','Update check or installation failed; retry')};
  let label=enabled?(labels[state]||labels.waiting):labels.disabled;
  if(last.latest&&state!=='available')label=last.latest+' · '+label;
  $('online-update-status').textContent=label;
  if(enabled&&last.available&&last.latest&&notified!==last.latest){notified=last.latest;options.toast(text('发现可用更新：','Update available: ')+last.latest);}
 }
 async function run(task){if(busy)return;busy=true;render();try{await task();}catch{options.toast(text('检查或升级失败，请重试','Update check or installation failed; retry'));}finally{busy=false;render();}}
 function begin(value){
  options=value;
  $('online-ota').addEventListener('change',()=>{if(busy)return;const selected=$('online-ota').checked;requested=selected;run(async()=>{try{await options.save({...options.config(),onlineOTA:selected});await options.refresh();}finally{requested=null;}});});
  $('update-check').addEventListener('click',()=>{if($('update-check').disabled)return;run(async()=>{
   if(last.available){
    if(!confirm(text('升级到 ','Update to ')+last.latest+text('？设备会重启，配置将保留。','? The controller will restart and keep its settings.')))return;
    await options.api('update/install',{version:last.latest,confirm:true});
   }else await options.api('update/check',{});
   await options.refresh();
  });});
  render();
 }
 return {begin,render};
})();
