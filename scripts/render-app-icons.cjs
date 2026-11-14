const fs=require('fs'),path=require('path'),sharp=require('sharp');
const root=path.resolve(__dirname,'..');
function svg(app,on=false,tray=false,n=1024){
 const palettes={connect:on?['#c8fff4','#65cbd1','#276574']:['#f2f5f7','#b9c1c8','#666e78'],screenshot:['#ffe1d4','#d69a9b','#8b5b68'],screencast:['#e4d5ff','#af92d8','#665086'],torrent:['#fff0d0','#ccb18a','#8e7052']};
 const [a,b,c]=palettes[app],w=Math.max(24,n<=48?1.25*1024/n:24),thin=Math.max(16,n<=48?1024/n:16);
 const defs=`<defs><linearGradient id="tile" x1="0" y1="0" x2="0.7" y2="1"><stop stop-color="#323438"/><stop offset=".38" stop-color="#16181c"/><stop offset="1" stop-color="#101216"/></linearGradient><linearGradient id="rim" x1="0" y1="0" x2="1" y2="1"><stop stop-color="${a}" stop-opacity=".8"/><stop offset=".3" stop-color="#55565e" stop-opacity=".3"/><stop offset=".7" stop-color="${c}" stop-opacity=".25"/><stop offset="1" stop-color="${b}" stop-opacity=".7"/></linearGradient><linearGradient id="ink" gradientUnits="userSpaceOnUse" x1="220" y1="200" x2="780" y2="850"><stop stop-color="${a}"/><stop offset=".5" stop-color="${b}"/><stop offset="1" stop-color="${c}"/></linearGradient><radialGradient id="dot" cx=".3" cy=".2" r="1"><stop stop-color="#f6b7b2"/><stop offset=".6" stop-color="#d88d91"/><stop offset="1" stop-color="#ad5f70"/></radialGradient></defs>`;
 const tile=tray?'':`<circle cx="512" cy="512" r="504" fill="url(#tile)" stroke="url(#rim)" stroke-width="7"/><circle cx="512" cy="512" r="492" fill="none" stroke="#ffffff" stroke-opacity=".035" stroke-width="3"/>`;
 let glyph='';
 if(app==='connect')glyph=`<path d="M 745 345 A 268 268 0 1 0 745 679" fill="none" stroke="url(#ink)" stroke-width="${tray?Math.max(55,w):48}" stroke-linecap="round"/><path d="M 563 512 H 791" fill="none" stroke="url(#ink)" stroke-width="${tray?Math.max(40,thin):thin}" stroke-linecap="round"/><circle cx="791" cy="512" r="${tray?31:22}" fill="${a}"/>`;
 if(app==='screenshot')glyph=`<path d="M 358 246 H 246 V 358 M 666 246 H 778 V 358 M 778 666 V 778 H 666 M 358 778 H 246 V 666" fill="none" stroke="url(#ink)" stroke-width="${w+3}" stroke-linejoin="round" stroke-linecap="round"/>`;
 if(app==='screencast')glyph=`<rect x="238" y="292" width="548" height="440" rx="9" fill="none" stroke="url(#ink)" stroke-width="${w}"/><circle cx="512" cy="512" r="78" fill="url(#dot)" stroke="#edc1c4" stroke-width="3"/>`;
 if(app==='torrent')glyph=`<path d="M 512 210 V 642 M 310 543 L 512 745 L 714 543 M 276 838 H 748" fill="none" stroke="url(#ink)" stroke-width="${w+4}" stroke-linecap="round" stroke-linejoin="round"/>`;
 return `<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024">${defs}${tile}${tray?'<g transform="translate(-120 -120) scale(1.24)">':''}${glyph}${tray?'</g>':''}</svg>`;
}
async function render(app,on,tray,n){return sharp(Buffer.from(svg(app,on,tray,n))).resize(n,n).png().toBuffer()}
async function write(p,b){fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b)}
function ico(frames){let off=6+16*frames.length;const h=Buffer.alloc(off);h.writeUInt16LE(1,2);h.writeUInt16LE(frames.length,4);frames.forEach(([n,b],i)=>{let p=6+i*16;h[p]=h[p+1]=n%256;h.writeUInt16LE(1,p+4);h.writeUInt16LE(32,p+6);h.writeUInt32LE(b.length,p+8);h.writeUInt32LE(off,p+12);off+=b.length});return Buffer.concat([h,...frames.map(x=>x[1])])}
(async()=>{
for(const app of ['screencast']){
 const repo=root,out=(p,b)=>write(path.join(repo,p),b),sizes=app==='connect'?[16,20,24,28,32,36,40,48,64,96,128,256]:app==='screencast'?[16,18,20,22,24,28,30,32,36,40,42,44,48,50,54,56,60,64,72,80,84,96,112,120,128,144,160,180,192,216,224,240,256]:app==='torrent'?[16,20,24,32,40,48,64,96,128,256]:[16,20,24,32,40,48,64,128,256];
 const frames=[];for(const n of sizes){let b=await render(app,false,false,n);frames.push([n,b]);if(app==='connect'){await out(`build/branding/icon-${n}.png`,b);await out(`build/branding/app-on-${n}.png`,await render(app,true,false,n));}if(app==='screencast')await out(`assets/icon-sizes/${n}.png`,b);if(app==='torrent')await out(`assets/icon-${n}.png`,b)}
 const icon=ico(frames),master=await render(app,false,false,1024),p256=frames.find(x=>x[0]===256)[1];
 if(app==='connect'){
  await out('build/branding/connect.svg',svg(app));await out('build/branding/source/app.svg',svg(app));await out('build/branding/source/app-on.svg',svg(app,true));
  for(const n of [512,1024])await out(`build/branding/icon-${n}.png`,await render(app,false,false,n));
  await out('build/branding/source/app-round.png',master);
  for(const state of ['off','on']){await out(`build/branding/source/tray-${state}.svg`,svg(app,state==='on',true));await out(`build/branding/source/tray-${state}.png`,await render(app,state==='on',true,1024));let tf=[];for(const n of sizes){let b=await render(app,state==='on',true,n);tf.push([n,b]);await out(`build/tray/${state}-${n}.png`,b)}await out(`build/tray/tray-${state}.ico`,ico(tf))}
  for(const p of ['build/windows/icon.ico','build/windows/installer.ico','installer-core/products/vpn.ico'])await out(p,icon);
  for(const [p,n] of [['build/appicon.png',1024],['cmd/installer/web/assets/icon.png',256],['frontend/src/assets/images/logo-universal.png',512],['extension/assets/icon.png',128],['installer-core/products/vpn.png',256],['installer-core/core/web/assets/icon.png',256]])await out(p,await render(app,false,false,n));
  for(const n of [16,24,32,48,64,128])await out(`extension/assets/icon-${n}.png`,await render(app,false,false,n));
  for(const p of ['build/tray/off-light.png','build/tray/off-dark.png'])await out(p,await render(app,false,true,32));await out('build/tray/on.png',await render(app,true,true,32));
  await out('build/tray/logo.svg',svg(app));await out('build/tray/logo-monochrome.svg',svg(app,false,true).replaceAll('url(#ink)','currentColor').replaceAll('#f2f5f7','currentColor'));
 } else if(app==='screenshot'){
  await out('assets/Screenshot.svg',svg(app));await out('assets/Screenshot.png',master);await out('assets/Screenshot-approved-original.png',master);await out('assets/Screenshot.ico',icon);
 } else if(app==='screencast'){
  await out('assets/Screencast.svg',svg(app));await out('assets/Screencast.ico',icon);await out('assets/Screencast-256.png',p256);for(const n of [2048,4096])await out(`assets/Screencast-${n}.png`,await render(app,false,false,n));await out('assets/Screencast-master.png',await render(app,false,false,4096));
  for(const v of ['0.2.5','0.2.6','0.4.3','0.4.4','0.4.5','0.4.6','0.4.7'])await out(`assets/Screencast-shell-${v}.ico`,icon);
 } else {await out('assets/icon.svg',svg(app));await out('assets/icon-master.png',master);await out('assets/icon-preview.png',p256);await out('assets/app.ico',icon)}
 console.log(app,'rendered',sizes.length,'frames');
}
})();
