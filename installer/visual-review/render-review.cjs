const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),{pathToFileURL}=require('url');
(async()=>{
 const root=__dirname,out=path.join(root,'screens');fs.mkdirSync(out,{recursive:true});
 const browser=await chromium.launch({executablePath:'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const page=await browser.newPage({viewport:{width:840,height:540},deviceScaleFactor:1});
 const errors=[];page.on('pageerror',e=>errors.push(e.message));
 const url=pathToFileURL(path.join(root,'index.html')).href;
 for(const language of ['ru','en'])for(const theme of ['light','dark'])for(const stage of ['welcome','settings','installing','finish']){
  await page.goto(url+`?stage=${stage}&theme=${theme}&lang=${language}`);await page.evaluate(()=>document.fonts.ready);
  await page.locator('#'+stage).waitFor({state:'visible'});
  const inspection=await page.evaluate(()=>{
   const shell=document.querySelector('.shell').getBoundingClientRect(),fonts=document.fonts.check('14px Montserrat');
   const bad=[...document.querySelectorAll('button,input,h1,h2,footer')].filter(e=>e.getClientRects().length).filter(e=>{const r=e.getBoundingClientRect();return r.left<shell.left||r.top<shell.top||r.right>shell.right+1||r.bottom>shell.bottom+1;}).map(e=>e.id||e.tagName);
   return {fonts,bad,version:document.querySelector('#version').textContent,extension:/Chrome|расширени|extension/i.test(document.body.innerText)};
  });
  if(!inspection.fonts||inspection.bad.length||inspection.extension||!inspection.version.includes('0.3.7'))throw Error(JSON.stringify(inspection));
  await page.screenshot({path:path.join(out,`${language}-${theme}-${stage}.png`),omitBackground:true});
 }
 await page.goto(url+'?stage=settings');await page.locator('#en').click();if(await page.locator('#settingsTitle').innerText()!=='Installation settings')throw Error('English switch failed');
 await page.locator('#dark').click();if(await page.getAttribute('html','data-theme')!=='dark')throw Error('Dark switch failed');
 await page.locator('#desktop').uncheck();await page.locator('#startMenu').uncheck();
 await page.locator('#back').click();await page.locator('#settingsButton').click();if(await page.locator('#desktop').isChecked()||await page.locator('#startMenu').isChecked())throw Error('Preview selections did not persist');
 await page.locator('#ru').click();await page.locator('#light').click();await page.locator('#accept').click();await page.locator('#finish').waitFor({state:'visible'});await page.locator('#launch').uncheck();
 if(errors.length)throw Error(errors.join('\n'));
 for(const theme of ['light','dark']){
  const titles={welcome:'Главный экран',settings:'Настройки установки',installing:'Установка',finish:'Завершение'};
  const cards=Object.entries(titles).map(([stage,title])=>`<article><h2>${title}</h2><img src="${pathToFileURL(path.join(out,`ru-${theme}-${stage}.png`)).href}"></article>`).join('');
  await page.setViewportSize({width:1736,height:1220});
  await page.setContent(`<style>@font-face{font-family:Montserrat;src:url('${pathToFileURL(path.join(root,'assets/Montserrat.ttf')).href}')}*{box-sizing:border-box}body{margin:0;padding:20px;background:${theme==='dark'?'#121314':'#e3e1dc'};font-family:Montserrat,sans-serif;color:${theme==='dark'?'#edede9':'#202624'}}h1{font-size:18px;font-weight:500;margin:0 0 12px}main{display:grid;grid-template-columns:840px 840px;gap:16px}article{margin:0}h2{font-size:13px;font-weight:500;margin:0 0 8px}img{display:block;width:840px;height:540px}</style><h1>Screencast · установщик 0.3.7 · ${theme==='dark'?'тёмная':'светлая'} тема</h1><main>${cards}</main>`);
  await page.evaluate(()=>document.fonts.ready);await page.locator('img').last().waitFor();
  await page.screenshot({path:path.join(out,`review-${theme}.png`)});
 }
 fs.writeFileSync(path.join(out,'verification.txt'),'PASS: 16 RU/EN light/dark stage previews; Montserrat loaded; no clipped controls; version 0.3.7 on every screen; no extension content; working language/theme and shortcut/launch selections; preview-only navigation/progress. No installer/app build or release performed.\n');
 await browser.close();console.log('Visual review verified; screenshots saved under installer/visual-review/screens.');
})().catch(e=>{console.error(e);process.exit(1)});
