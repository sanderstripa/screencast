// Native installer presentation only; the approved HTML is the rendering source.
const {chromium}=require('playwright');
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{pathToFileURL}=require('url');
const {PNG}=require('pngjs');
(async()=>{
 const source=path.join(__dirname,'visual-review'),output=path.join(__dirname,'generated');fs.mkdirSync(output,{recursive:true});
 const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const scales=[96,120,144,168,192,240,288,384],stages=['welcome','settings','installing','finish'];
 const controls={1:'ru',12:'en',2:'light',13:'dark',3:'settingsButton',4:'primary',5:'desktop',6:'startMenu',7:'launch',8:'browse',10:'minimize',11:'close'};
 const layout={},assets=[];
 for(let d=0;d<scales.length;d++){
  const page=await browser.newPage({viewport:{width:840,height:540},deviceScaleFactor:scales[d]/96});
  for(let language=0;language<2;language++)for(let theme=0;theme<2;theme++)for(let stage=0;stage<4;stage++){
   await page.goto(pathToFileURL(path.join(source,'index.html')).href+`?stage=${stages[stage]}&theme=${theme?'dark':'light'}&lang=${language?'en':'ru'}`);
   await page.evaluate(()=>document.fonts.ready);
   await page.evaluate(()=>{document.querySelector('#path').value='';});
   const boxes=await page.evaluate(({controls,stage})=>{
    const result={};
    for(const [id,selector]of Object.entries(controls)){
     let target=selector;
     if(id==='3')target=stage===0?'settingsButton':stage===1?'back':'';
     if(id==='4')target=stage===0?'primary':stage===1?'accept':stage===3?'done':'';
     if(!target)continue;let element=document.getElementById(target);
     if(!element.getClientRects().length)continue;
     if(['5','6','7'].includes(id))element=element.closest('label');
     const r=element.getBoundingClientRect();result[id]=[r.left,r.top,r.right,r.bottom];
    }
    for(const [id,selector]of [[14,'path'],[15,'progress']]){
     const element=document.getElementById(selector);if(!element.getClientRects().length)continue;
     const r=element.getBoundingClientRect();result[id]=[r.left,r.top,r.right,r.bottom];
     if(id===14)result[9]=[r.left+12,r.top+9,r.right-12,r.bottom-9];
    }
    return result;
   },{controls,stage});
   if(d===0&&theme===0)layout[`${language}-${stage}`]=boxes;
   const baseId=1000+d*16+language*8+theme*4+stage,file=`page-${scales[d]}-${language}-${theme}-${stage}.png`;
   const bytes=await page.screenshot({omitBackground:true});fs.writeFileSync(path.join(output,file),bytes);
   assets.push({id:baseId,file,dpi:scales[d],language,theme,stage});
   if(stage===1||stage===3){
    await page.evaluate(()=>{for(const id of ['desktop','startMenu','launch'])document.getElementById(id).checked=false;});
    const pixels=PNG.sync.read(await page.screenshot({omitBackground:true}));
    for(const control of stage===1?[5,6]:[7]){
     const scale=scales[d]/96,r=boxes[control],left=Math.floor(r[0]*scale),top=Math.floor(r[1]*scale),width=Math.ceil(r[2]*scale)-left,height=Math.ceil(r[3]*scale)-top;
     const tile=new PNG({width,height});PNG.bitblt(pixels,tile,left,top,width,height,0,0);
     const id=2000+d*12+language*6+theme*3+(control-5),file=`off-${scales[d]}-${language}-${theme}-${control}.png`;
     fs.writeFileSync(path.join(output,file),PNG.sync.write(tile));assets.push({id,file,dpi:scales[d],language,theme,stage,control});
    }
   }
  }
  await page.close();
 }
 await browser.close();
 const header=['// Generated from the approved installer visual-review DOM.','#pragma once','namespace installerVisual {','inline constexpr int width=840,height=540;','struct Box{double left,top,right,bottom;};','inline Box box(bool english,int stage,int control){switch((english?1000:0)+stage*100+control){'];
 for(const [key,boxes]of Object.entries(layout)){const [language,stage]=key.split('-').map(Number);for(const [control,values]of Object.entries(boxes))header.push(`case ${language*1000+stage*100+Number(control)}:return {${values.map(v=>Number(v).toFixed(6)).join(',')}};`);}
 header.push('default:return {};}}','inline int scaleIndex(int dpi){switch(dpi){');
 for(let i=0;i<scales.length;i++)header.push(`case ${scales[i]}:return ${i};`);
 header.push('default:return 7;}}','inline int base(int dpi,bool english,bool dark,int stage){return 1000+scaleIndex(dpi)*16+(english?8:0)+(dark?4:0)+stage;}','inline int off(int dpi,bool english,bool dark,int control){return 2000+scaleIndex(dpi)*12+(english?6:0)+(dark?3:0)+control-5;}','}');
 fs.writeFileSync(path.join(output,'layout.h'),header.join('\n')+'\n');
 fs.writeFileSync(path.join(output,'assets.rc.in'),assets.map(a=>`${a.id} RCDATA "@CMAKE_CURRENT_SOURCE_DIR@/installer/generated/${a.file}"`).join('\n')+'\n');
 const sources={};for(const file of ['index.html','style.css','preview.js','assets/Montserrat.ttf','assets/ribbon.png'])sources[file]=crypto.createHash('sha256').update(file.match(/\.(html|css|js)$/)?fs.readFileSync(path.join(source,file),'utf8').replace(/\r\n/g,'\n'):fs.readFileSync(path.join(source,file))).digest('hex');
 for(const asset of assets)asset.sha256=crypto.createHash('sha256').update(fs.readFileSync(path.join(output,asset.file))).digest('hex');
 fs.writeFileSync(path.join(output,'manifest.json'),JSON.stringify({version:'0.4.1',sources,layout,assets},null,2)+'\n');
 console.log(`Generated ${assets.length} native installer resources at ${scales.join('/')} DPI.`);
})().catch(e=>{console.error(e);process.exit(1)});
