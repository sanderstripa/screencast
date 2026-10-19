const fs=require('fs'),path=require('path'),http=require('http'),assert=require('assert');
const {chromium}=require('playwright');
const root=__dirname;
const properties=['fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','borderTopWidth','borderRightWidth','borderBottomWidth','borderLeftWidth','borderRadius','paddingTop','paddingRight','paddingBottom','paddingLeft','opacity','transform','zoom','filter','backdropFilter','willChange','color','backgroundColor'];
(async()=>{
 const server=http.createServer((req,res)=>{
  let route=new URL(req.url,'http://localhost').pathname;
  const reference=route.startsWith('/reference/');
  let file=path.join(root,reference?'connect-reference':'visual-review',route.replace(/^\/(reference|actual)\//,''));
  if(route.includes('/assets/'))file=path.join(root,'visual-review','assets',path.basename(route)==='mobius.png'?'ribbon.png':path.basename(route));
  if(route.includes('/wails/')){res.end('');return;}
  try{let body=fs.readFileSync(file);if(reference&&file.endsWith('index.html'))body=Buffer.from(body.toString().replace('__TOKEN__','test').replace('__MODE__','false').replace('__CONFIG__',JSON.stringify({path:'C:\\Program Files\\Connect',version:'0.2.5'})).replace('<script type="module" src="/wails/runtime.js"></script>','').replace('src="setup.js"','src="/reference/setup.js"'));
   res.setHeader('Content-Type',file.endsWith('.css')?'text/css':file.endsWith('.js')?'text/javascript':file.endsWith('.ttf')?'font/ttf':file.endsWith('.png')?'image/png':'text/html');res.end(body);
  }catch{res.statusCode=404;res.end();}
 });await new Promise(r=>server.listen(0,'127.0.0.1',r));
 const browser=await chromium.launch({executablePath:process.env.CHROME_PATH||'C:/Program Files/Google/Chrome/Application/chrome.exe',headless:true});
 const pages=await Promise.all(['reference','actual'].map(async name=>{let p=await browser.newPage({viewport:{width:840,height:540}});await p.goto(`http://127.0.0.1:${server.address().port}/${name}/index.html`);await p.evaluate(()=>document.fonts.ready);return p;}));
 const measure=async(page,reference,stage,locale,theme)=>{
  await page.evaluate(`${reference?'lang':'language'}=${JSON.stringify(locale)}; ${reference?'light='+String(theme==='light'):'theme='+JSON.stringify(theme)}; stage=${JSON.stringify(stage)}; ${reference?'installed=true;chromeReady=true;':''}render();`);
  return page.evaluate(({properties})=>{const out={};for(const id of ['settingsTitle','languageLabel','pathLabel','path','browse','accept','done','success']){const el=document.getElementById(id),r=el.getBoundingClientRect(),s=getComputedStyle(el);out[id]={rect:[r.x,r.y,r.width,r.height],style:Object.fromEntries(properties.map(k=>[k,s[k]]))};}const h=document.querySelector('#finish h1'),r=h.getBoundingClientRect();out.title={rect:[r.x,r.y,r.width,r.height]};return out;},{properties});
 };
 const report=[];
 for(const locale of ['ru','en'])for(const theme of ['light','dark'])for(const stage of ['settings','finish']){
  const reference=await measure(pages[0],true,stage,locale,theme),actual=await measure(pages[1],false,stage,locale,theme);
  report.push({locale,theme,stage,reference,actual});
  if(process.argv.includes('--measure'))continue;
  for(const id of stage==='settings'?['settingsTitle','languageLabel','pathLabel','path','browse','accept']:['done','success']){
   assert.deepStrictEqual(actual[id].style,reference[id].style,`${locale}/${theme}/${stage}/${id}: computed CSS differs from CONNECT`);
   assert.strictEqual(actual[id].rect[1],reference[id].rect[1],`${id}: vertical position differs`);
   if(id!=='success')assert.deepStrictEqual(actual[id].rect.slice(2),reference[id].rect.slice(2),`${id}: control dimensions differ`);
  }
  if(stage==='finish'){assert.strictEqual(actual.title.rect[1],reference.title.rect[1],'Final title top differs');assert.strictEqual(actual.title.rect[3],reference.title.rect[3],'Final title height differs');}
 }
 fs.mkdirSync(path.join(root,'generated'),{recursive:true});fs.writeFileSync(path.join(root,'generated','connect-parity.json'),JSON.stringify(report,null,2)+'\n');
 console.log(process.argv.includes('--measure')?JSON.stringify(report.slice(0,2).map(x=>({stage:x.stage,reference:Object.fromEntries(Object.entries(x.reference).map(([k,v])=>[k,v.rect])),actual:Object.fromEntries(Object.entries(x.actual).map(([k,v])=>[k,v.rect]))})),null,2):'PASS: exact CONNECT computed CSS and geometry in RU/EN light/dark settings and completion.');
 await browser.close();await new Promise(r=>server.close(r));
})().catch(e=>{console.error(e);process.exit(1)});
