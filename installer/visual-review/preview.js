// Design review only. No installation, process launches, filesystem or registry writes.
const $=id=>document.getElementById(id),params=new URLSearchParams(location.search);
let language=params.get('lang')==='en'?'en':'ru',theme=params.get('theme')==='dark'?'dark':'light',stage=params.get('stage')||'welcome';
const copy={ru:{eyebrow:'УСТАНОВКА',primary:'Установить',settingsButton:'Настройки установки',settingsTitle:'Настройки установки',back:'Назад',languageLabel:'Язык',themeLabel:'Тема',light:'Светлая',dark:'Тёмная',pathLabel:'Папка установки',browse:'Выбрать…',desktopLabel:'Ярлык на рабочем столе',menuLabel:'Ярлык в меню «Пуск»',accept:'Принять и установить',status:'Установка…',success:'Установка завершена',done:'Готово',launchLabel:'Запустить Screencast после установки',version:'Версия 0.4.0',minimize:'Свернуть',close:'Закрыть'},en:{eyebrow:'INSTALLATION',primary:'Install',settingsButton:'Installation settings',settingsTitle:'Installation settings',back:'Back',languageLabel:'Language',themeLabel:'Theme',light:'Light',dark:'Dark',pathLabel:'Installation folder',browse:'Browse…',desktopLabel:'Desktop shortcut',menuLabel:'Start menu shortcut',accept:'Accept and install',status:'Installing…',success:'Installation complete',done:'Done',launchLabel:'Launch Screencast after installation',version:'Version 0.4.0',minimize:'Minimize',close:'Close'}};
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
