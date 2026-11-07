'use strict';
window.MatrixForms=(()=>{
 const tracked=new Map(),locks=new Map(),protectedFields=new Map(),standalone=new Map();
 const t=(zh,en)=>MatrixI18n.text(zh,en);
 function value(input){return input.type==='checkbox'?input.checked:input.value;}
 function dirty(input){return tracked.has(input)&&value(input)!==tracked.get(input);}
 function heading(input){return input.closest('label,.field');}
 function lockState(input,locked){input.dataset.locked=locked?'1':'0';input.setAttribute('aria-readonly',String(locked));if(input.tagName==='INPUT')input.readOnly=locked;heading(input)?.classList.toggle('editLocked',locked);input.title=locked?t('双击修改；键盘按 F2 或 Enter','Double-click to edit; F2 or Enter with keyboard'):'';validate();}
 function lock(input,saved){
  if(!locks.has(input)){
   const unlock=()=>{lockState(input,false);input.focus();};
   input.addEventListener('dblclick',unlock);
   input.addEventListener('keydown',e=>{if(input.dataset.locked!=='1')return;if(e.key==='F2'||e.key==='Enter'){e.preventDefault();unlock();}else if(input.tagName==='SELECT'&&e.key!=='Tab')e.preventDefault();});
   input.addEventListener('pointerdown',e=>{if(input.tagName==='SELECT'&&input.dataset.locked==='1')e.preventDefault();});
   input.addEventListener('change',()=>{if(input.dataset.locked==='1'){input.value=tracked.get(input);validate();}});
   input.addEventListener('blur',()=>lockState(input,locks.get(input)&&!dirty(input)));
  }
  locks.set(input,!!saved);tracked.set(input,value(input));input.classList.remove('unsaved');lockState(input,!!saved);
 }
 function protect(inputId,policyId){
  const input=document.getElementById(inputId),policy=document.getElementById(policyId);
  protectedFields.set(input,{policy,saved:false,encrypted:true});
  const update=()=>{policy.disabled=!input.value.length;};input.addEventListener('input',update);update();
 }
 function credential(inputId,saved,encrypted=true){
  const input=document.getElementById(inputId),p=protectedFields.get(input);
  if(p){p.saved=!!saved;p.encrypted=encrypted;p.policy.checked=encrypted;p.policy.disabled=!input.value.length;tracked.set(p.policy,encrypted);p.policy.classList.remove('unsaved');}
  input.placeholder=saved?t('已保存，留空保留','Saved; leave blank to keep'):'';lock(input,!!saved);
 }
 function saved(root){for(const input of root.querySelectorAll('input,select')){if(['file','password'].includes(input.type))continue;tracked.set(input,value(input));input.classList.remove('unsaved');}}
 function validate(){
  for(const id of ['admin-password','first-password']){const input=document.getElementById(id);input.setCustomValidity(input.value==='12345678'?t('新密码不能使用初始密码','Choose a password different from the initial password'):'');}
  const confirmation=document.getElementById('first-confirm');confirmation.setCustomValidity(confirmation.value&&confirmation.value!==document.getElementById('first-password').value?t('两次输入的密码不一致','Passwords do not match'):'');
  for(const form of document.forms){
   if(form.id==='login-form'){form.querySelector('button').disabled=form.dataset.busy==='1';continue;}
   for(const input of form.querySelectorAll('input,select')){input.classList.toggle('unsaved',dirty(input));const h=heading(input);h?.classList.toggle('requiredField',input.required&&!input.readOnly&&!input.disabled);}
   const changed=Array.from(form.querySelectorAll('input,select')).some(dirty);
   const needsChanges=['printer-form','pixel-form','wifi-form'].includes(form.id);
   for(const button of form.querySelectorAll('button:not([type]),button[type=submit]'))button.disabled=form.dataset.busy==='1'||!form.checkValidity()||(needsChanges&&!changed);
  }
  for(const [button,inputs] of standalone){for(const input of inputs)input.classList.toggle('unsaved',dirty(input));button.disabled=button.dataset.busy==='1'||!inputs.some(dirty)||!inputs.every(input=>input.checkValidity());}
 }
 function refresh(){for(const input of locks.keys())if(input.dataset.locked==='1')lockState(input,true);for(const [input,p] of protectedFields){p.policy.disabled=!input.value.length;p.policy.title=t('加密保存并绑定当前设备密钥；不勾选则明文保存。','Encrypt and bind to this device key; unchecked stores plaintext.');if(!input.value)input.placeholder=p.saved?t('已保存，留空保留','Saved; leave blank to keep'):'';}validate();}
 function begin(){
  const timezone=document.getElementById('timezone');standalone.set(document.getElementById('timezone-save'),[timezone]);for(const event of ['input','change'])timezone.addEventListener(event,validate);
  for(const form of document.forms)for(const event of ['input','change'])form.addEventListener(event,validate);
  for(const [input,policy] of [['printer-code','printer-encrypt'],['pixel-password','pixel-encrypt'],['wifi-password','wifi-encrypt'],['cloud-secret','cloud-encrypt']])protect(input,policy);
  validate();
 }
 return {begin,lock,credential,saved,validate,refresh,edit:input=>lockState(input,false),invalidate:input=>{locks.set(input,false);input.placeholder='';lockState(input,false);}};
})();
