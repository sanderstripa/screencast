// Design review only. No installation, process launches, filesystem or registry writes.
const $=id=>document.getElementById(id),params=new URLSearchParams(location.search);
let language=params.get('lang')==='en'?'en':'ru',theme=params.get('theme')==='dark'?'dark':'light',stage=params.get('stage')||'welcome';
const copy={ru:{eyebrow:'УСТАНОВКА',primary:'Установить',settingsButton:'Настройки установки',settingsTitle:'Настройки установки',back:'Назад',languageLabel:'Язык',themeLabel:'Тема',light:'Светлая',dark:'Тёмная',pathLabel:'Папка установки',browse:'Выбрать…',desktopLabel:'Ярлык на рабочем столе',menuLabel:'Ярлык в меню «Пуск»',accept:'Принять и установить',status:'Установка…',success:'Установка завершена',done:'Готово',launchLabel:'Запустить Screencast после установки',version:'Версия 0.4.1',minimize:'Свернуть',close:'Закрыть'},en:{eyebrow:'INSTALLATION',primary:'Install',settingsButton:'Installation settings',settingsTitle:'Installation settings',back:'Back',languageLabel:'Language',themeLabel:'Theme',light:'Light',dark:'Dark',pathLabel:'Installation folder',browse:'Browse…',desktopLabel:'Desktop shortcut',menuLabel:'Start menu shortcut',accept:'Accept and install',status:'Installing…',success:'Installation complete',done:'Done',launchLabel:'Launch Screencast after installation',version:'Version 0.4.1',minimize:'Minimize',close:'Close'}};
function render(){
 document.documentElement.lang=language;document.documentElement.dataset.theme=theme;document.body.dataset.stage=stage;
 for(const name of ['welcome','settings','installing','finish'])$(name).hidden=name!==stage;
 for(const [id,value]of Object.entries(copy[language])){if(id==='minimize'||id==='close')$(id).setAttribute('aria-label',value);else $(id).textContent=value;}
 for(const id of ['ru','en'])$(id).setAttribute('aria-pressed',String(id===language));
 for(const id of ['light','dark'])$(id).setAttribute('aria-pressed',String(id===theme));
 $('close').disabled=stage==='installing';
}
function go(next){stage=next;render();}
let timer;
function demonstrateProgress(){clearInterval(timer);go('installing');$('progress').value=0;timer=setInterval(()=>{if(stage!=='installing'){clearInterval(timer);return;}$('progress').value+=2;if($('progress').value===100){clearInterval(timer);go('finish');}},60);}
$('settingsButton').onclick=()=>go('settings');$('back').onclick=()=>go('welcome');
$('primary').onclick=demonstrateProgress;$('accept').onclick=demonstrateProgress;
for(const id of ['ru','en'])$(id).onclick=()=>{language=id;render();};
for(const id of ['light','dark'])$(id).onclick=()=>{theme=id;render();};
$('browse').onclick=()=>{$('path').focus();$('path').select();};
$('done').onclick=()=>go('welcome');
window.installerReview={go,get state(){return {language,theme,stage};}};
render();

// Native installation bridge; preview mode above remains available for visual review.
if(window.chrome?.webview){
 const send=message=>window.chrome.webview.postMessage(message);
 const controls={ru:1,en:12,light:2,dark:13,settingsButton:3,back:3,primary:4,accept:4,done:4,desktop:5,startMenu:6,launch:7,browse:8,minimize:10,close:11};
 for(const [id,command] of Object.entries(controls)){
  const el=$(id),event=el instanceof HTMLInputElement?'onchange':'onclick';
  el[event]=()=>{send('path:'+$('path').value);send('command:'+command);};
 }
 $('path').oninput=()=>send('path:'+$('path').value);
 document.addEventListener('pointerdown',event=>{if(event.button===0&&!event.target.closest('button,input,label,select,textarea'))send('drag');});
 window.applyInstallerState=state=>{
  language=state.english?'en':'ru';theme=state.dark?'dark':'light';stage=['welcome','settings','installing','finish'][state.stage];
  if($('path').value!==state.path)$('path').value=state.path;
  $('desktop').checked=!!state.desktop;$('startMenu').checked=!!state.startMenu;$('launch').checked=!!state.launch;
  $('progress').value=state.progress;render();
 };
}
