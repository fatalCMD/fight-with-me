const fs=require('fs'),path=require('path'),zlib=require('zlib');
const root=path.resolve(__dirname,'../..'),game=path.resolve(root,'../STOCK GAME/Data'),out=path.join(root,'build/skald-research');fs.mkdirSync(out,{recursive:true});
const data=fs.readFileSync(path.join(game,'Skyrim.esm')),records=[];
function walk(start,end){for(let at=start;at+24<=end;){const type=data.toString('ascii',at,at+4),size=data.readUInt32LE(at+4);if(type==='GRUP'){walk(at+24,at+size);at+=size;continue;}if(['SNDR','IDLE','ANIO','MISC'].includes(type)){let b=data.subarray(at+24,at+24+size);if(data.readUInt32LE(at+8)&0x40000)b=zlib.inflateSync(b.subarray(4));let ed='',fields=[];for(let p=0;p+6<=b.length;){const tag=b.toString('ascii',p,p+4),n=b.readUInt16LE(p+4),v=b.subarray(p+6,p+6+n);if(tag==='EDID')ed=v.toString('utf8').replace(/\0/g,'');fields.push({tag,text:v.toString('utf8').replace(/\0/g,''),hex:v.toString('hex')});p+=6+n;}if(/lute/i.test(ed))records.push({type,id:data.readUInt32LE(at+12),ed,fields});}at+=24+size;}}walk(0,data.length);
fs.writeFileSync(path.join(out,'records.json'),JSON.stringify(records,null,2));console.log(JSON.stringify(records.map(x=>({type:x.type,id:x.id.toString(16),ed:x.ed,fields:x.fields.filter(f=>['ANAM','MODL','DNAM','ENAM'].includes(f.tag))}))));
const {open}=require('../wolfhead/bsa.js');const bsa=open(path.join(game,'Skyrim - Sounds.bsa'));for(const f of bsa.files.filter(x=>/mus_bardlute_\d+.wav$/i.test(x.path))){const bytes=bsa.read(f);fs.writeFileSync(path.join(out,f.name),bytes);console.log(JSON.stringify({file:f.name,bytes:bytes.length,header:bytes.subarray(0,80).toString('hex')}));}
const meshes=open(path.join(game,'Skyrim - Meshes0.bsa'));
const lute=meshes.files.find(f=>f.path.toLowerCase().endsWith('animobjects\\animobjectlute.nif'));
if(!lute)throw Error('Stock Skyrim animation lute mesh was not found');
fs.writeFileSync(path.join(out,'lute.nif'),meshes.read(lute));

