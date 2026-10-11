import fs from 'fs'; import path from 'path'; import {execFileSync} from 'child_process';
const ROOT='C:/Users/bgbg0/Documents/Programming/roadworld/unreal/CityPainter/SourceArt/Free';
const today='2026-10-11';
const acg={ // category -> [ids], res override
 'surfaces/asphalt':['Road012A:8K','Road014A','Asphalt025C','Asphalt032','Asphalt024A'],
 'surfaces/asphalt_worn_patched':['Road009C','Road015C','Asphalt019','Asphalt016','Road001','Road003','Road005','Asphalt027C','Asphalt021','Asphalt026C'],
 'decals':['RoadLines006','RoadLines007','RoadLines008','RoadLines009','RoadLines010','RoadLines011','Grate001','Grate002'],
 'footway':['TactilePaving002','PavingStones085','PavingStones087','PavingStones088','PavingStones089'],
 'rock':['Rock030','Rock028','Rock031','Rock037','Rock048','Rock022','Rock023','Rock051','Rock026','Rock018','Rock034'],
 'ground':['Grass004','Grass005','Grass007','Ground037','Ground003','Ground085','Ground067','Gravel041','Gravel040','Ground062S','Ground110','Ground081'],
};
globalThis.RES='2k';const extra=process.argv[2]?JSON.parse(fs.readFileSync(process.argv[2],'utf8')):null;
const log=(...a)=>{const s=a.join(' ');console.log(s);fs.appendFileSync('dl.log',s+'\n')};
import https from 'https';
function once(url,dest,redir=0){return new Promise((res,rej)=>{const have=fs.existsSync(dest)?fs.statSync(dest).size:0;const u=new URL(url);const req=https.get(u,{headers:{'User-Agent':'roadworld-asset-research',...(have?{Range:'bytes='+have+'-'}:{})}},r=>{if([301,302,303,307,308].includes(r.statusCode)){r.resume();if(redir>5)return rej(new Error('redir'));return once(new URL(r.headers.location,u).href,dest,redir+1).then(res,rej)}
if(r.statusCode===416){r.resume();return res(have)}
if(r.statusCode!==200&&r.statusCode!==206){r.resume();return rej(new Error('HTTP '+r.statusCode))}
const append=r.statusCode===206;const ws=fs.createWriteStream(dest,{flags:append?'a':'w'});r.on('error',rej);r.pipe(ws);ws.on('finish',()=>{const sz=fs.statSync(dest).size;const tot=Number((r.headers['content-range']||'').split('/')[1]||r.headers['content-length']||0)+(append?0:0);res(sz)});ws.on('error',rej)});req.setTimeout(120000,()=>req.destroy(new Error('timeout')));req.on('error',rej)})}
async function get(url,dest){fs.mkdirSync(path.dirname(dest),{recursive:true});for(let t=0;t<40;t++){try{return await once(url,dest)}catch(e){await new Promise(r=>setTimeout(r,3000))}}throw new Error('gave up '+url)}
async function doAcg(cat,spec){const [id,res='4K']=spec.split(':');const dir=path.join(ROOT,cat,'ambientCG',id);if(fs.existsSync(path.join(dir,'LICENCE.txt'))){return}
 const info=await (await fetch(`https://ambientcg.com/api/v2/full_json?id=${id}&include=downloadData`)).json();
 const a=info.foundAssets[0];const d=a.downloadFolders.default.downloadFiletypeCategories.zip.downloads.find(x=>x.attribute===res+'-JPG');
 const zip=path.join(dir,'_dl.zip');const n=await get(d.fullDownloadPath,zip);
 // extract images only
 let list;try{list=execFileSync('unzip',['-Z1',zip],{encoding:'utf8'})}catch(e){fs.unlinkSync(zip);throw e}list=list.split(/\r?\n/).filter(f=>/\.(jpg|png)$/i.test(f));
 execFileSync('unzip',['-o','-q',zip,...list,'-d',dir]);fs.unlinkSync(zip);
 fs.writeFileSync(path.join(dir,'LICENCE.txt'),`Asset: ${id} (${res} JPG)\nSource: https://ambientcg.com/view?id=${id}\nLicence: CC0 1.0 Universal ("All ambientCG assets are provided under the Creative Commons CC0 1.0 Universal License." https://docs.ambientcg.com/license/)\nCommercial use: allowed, no attribution required\nDownloaded: ${today}\nNote: .blend/.mtlx/.tres files in the original zip were not extracted (images only).\n`);
 log('ACG ok',cat,id,res,Math.round(n/1e6)+'MB');}
async function doPH(cat,id,type){const dir=path.join(ROOT,cat,'PolyHaven',id);if(fs.existsSync(path.join(dir,'LICENCE.txt')))return;
 const f=await (await fetch('https://api.polyhaven.com/files/'+id,{headers:{'User-Agent':'roadworld-asset-research'}})).json();let tot=0;
 const pick=(o,res)=>o[res]||o['4k']||o['2k']||o['1k'];
 if(type==='tex'){const maps={Diffuse:'diff',nor_dx:'nor_dx',Rough:'rough',AO:'ao',Displacement:'disp',arm:'arm'};for(const [k,short] of Object.entries(maps)){const m=f[k];if(!m)continue;const want=(globalThis.RES||'4k');const r=m[want]||m['2k'];const rk=m[want]?want:'2k';const u=r.jpg||r.png;if(!u)continue;tot+=await get(u.url,path.join(dir,`${id}_${short}_${rk}.jpg`))}}
 else if(type==='hdri'){const r=f.hdri['4k']||f.hdri['2k'];const u=r.hdr||r.exr;tot+=await get(u.url,path.join(dir,id+'_4k.hdr'))}
 else if(type==='model'){const fb=f.fbx&&(f.fbx['4k']||f.fbx['2k']||f.fbx['1k']);if(!fb)throw new Error('no fbx');const rk=f.fbx['4k']?'4k':f.fbx['2k']?'2k':'1k';tot+=await get(fb.fbx.url,path.join(dir,id+'_'+rk+'.fbx'));for(const [p,v] of Object.entries(fb.fbx.include||{}))tot+=await get(v.url,path.join(dir,p))}
 fs.writeFileSync(path.join(dir,'LICENCE.txt'),`Asset: ${id}\nSource: https://polyhaven.com/a/${id}\nLicence: CC0 (https://polyhaven.com/license: "All assets on Poly Haven are licensed under CC0")\nCommercial use: allowed, no attribution required\nDownloaded: ${today}\n`);
 log('PH ok',cat,id,type,Math.round(tot/1e6)+'MB');}
const jobs=[];
if(!extra){for(const [c,l] of Object.entries(acg))for(const s of l)jobs.push(()=>doAcg(c,s));}
else{for(const g of extra.order){if(g.acg)jobs.push(()=>doAcg(g.c,g.acg));else jobs.push(()=>doPH(g.c,g.ph,g.t));}}
let i=0;async function w(){while(i<jobs.length){const j=jobs[i++];try{await j()}catch(e){log('FAIL',String(e))}}}
await Promise.all([w(),w(),w()]);log('DONE');
