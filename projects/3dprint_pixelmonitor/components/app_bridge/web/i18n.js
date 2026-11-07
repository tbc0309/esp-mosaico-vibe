'use strict';
window.MatrixI18n=(()=>{
 const {codes,words}=MatrixLocales;
 let active='en';
 function language(){return active;}
 function set(value){active=codes.includes(value)?value:'en';}
 function browser(){
  for(const value of navigator.languages||[navigator.language]){
   const code=value.toLowerCase().replaceAll('_','-');
   if(/^zh(?:-|$)/.test(code))return /^zh-(?:tw|hk|mo|hant)(?:-|$)/.test(code)?'zh-TW':'zh';
   const base=code.split('-')[0];if(codes.includes(base))return base;
  }
  return 'en';
 }
 function text(zh,en){if(language()==='zh')return zh;const row=words[en];return row?row[codes.indexOf(language())]:en;}
 function retranslate(value){
  if(!value)return value;
  for(const [key,row] of Object.entries(words))if(row.includes(value))return row[codes.indexOf(language())];
  for(const row of Object.values(words))for(const prefix of row)if(prefix&&/[:：]\s*$/.test(prefix)&&value.startsWith(prefix))return row[codes.indexOf(language())]+value.slice(prefix.length);
  return value;
 }
 return {language,set,browser,text,retranslate,codes};
})();
