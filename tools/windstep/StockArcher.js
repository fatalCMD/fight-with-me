// Bake Skyrim's own armour and body meshes into one rigid, posed spell silhouette.
// Source archives are read locally. No full NPC, AI, collision, facegen or actor clone is spawned.
const path=require('path'),fs=require('fs'),assert=require('assert');
const {open}=require('../wolfhead/bsa'),nif=require('../wolfhead/nifio');
const {piece,vertex,add,sub,mul,norm,dot,cross}=require('../fx/meshkit');
const data=process.env.FWM_SKYRIM_DATA||'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const archives=[0,1].map(i=>open(path.join(data,`Skyrim - Meshes${i}.bsa`)));
const apply=(r,v)=>[0,1,2].map(k=>r[k*3]*v[0]+r[k*3+1]*v[1]+r[k*3+2]*v[2]);
const product=(a,b)=>Array.from({length:9},(_,i)=>[0,1,2].reduce((s,k)=>s+a[Math.floor(i/3)*3+k]*b[k*3+i%3],0));
function load(file){for(const a of archives){const f=a.files.find(f=>f.path.replaceAll('\\','/').toLowerCase()===file.toLowerCase());if(f)return nif.read(a.read(f));}throw Error('Missing stock mesh '+file);}
function dynamic(b){
 if(b.type!=='BSDynamicTriShape')return b;
 const r=new nif.Reader(b.raw),s={type:b.type,name:r.i32()};r.refs();r.i32();r.u32();
 s.translate=Array.from({length:3},()=>r.f32());s.rotate=Array.from({length:9},()=>r.f32());s.scale=r.f32();r.i32();
 r.p+=16;s.skin=r.i32();s.shader=r.i32();s.alpha=r.i32();s.desc=r.u64();r.u16();const count=r.u16(),bytes=r.u32();
 assert.equal(bytes,0,'Skinned dynamic face geometry must live in its partition');assert.equal(r.u32(),0,'unexpected face particles');
 assert.equal(r.u32(),count*16,'dynamic face position buffer');
 s.dynamic=Array.from({length:count},()=>{const p=[r.f32(),r.f32(),r.f32()];r.f32();return p;});assert.equal(r.p,b.raw.length);return s;
}
function prepare(source){
 const parent=new Map(),world=new Map();source.blocks.forEach((b,i)=>b.children?.forEach(c=>parent.set(c,i)));
 function rest(i){if(world.has(i))return world.get(i);const b=source.blocks[i],p=parent.has(i)?rest(parent.get(i)):null;
  const v=p?{r:product(p.r,b.rotate),t:add(mul(apply(p.r,b.translate),p.s),p.t),s:p.s*b.scale}:{r:b.rotate,t:b.translate,s:b.scale};world.set(i,v);return v;
 }
 return {source,rest,name:b=>source.strings[b.name]};
}
function poseFor(label){
 const frames=JSON.parse(fs.readFileSync(path.resolve(__dirname,'../../build/windstep-stock/pose.json'),'utf8'));
 assert.equal(frames.animation,'bow_idledrawn.hkx');
 const bones=frames[label],world=[],result=new Map();
 function at(i){if(world[i])return world[i];const b=bones[i],l=Math.hypot(...b.q),[x,y,z,w]=b.q.map(v=>v/l);
  const r=[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)];
  assert(Math.max(...b.s)-Math.min(...b.s)<.001,'uniform stock bone scale');
  const p=b.parent>=0?at(b.parent):null,s=b.s[0];
  return world[i]=p?{r:product(p.r,r),t:add(p.t,mul(apply(p.r,b.t),p.s)),s:p.s*s}:{r,t:b.t,s};
 }
 bones.forEach((b,i)=>result.set(b.name,at(i)));return result;
}
function shapes(file,pose=new Map()){
 const {source,rest,name}=file,result=[];
 for(let index=0;index<source.blocks.length;index++){
  const block=source.blocks[index];if(!['BSTriShape','BSDynamicTriShape'].includes(block.type))continue;const s=dynamic(block);if(s.skin<0)continue;
  const skin=new nif.Reader(source.blocks[s.skin].raw),di=skin.i32(),pi=skin.i32();skin.i32();const bones=skin.refs();
  const reader=new nif.Reader(source.blocks[di].raw),transform=()=>({r:Array.from({length:9},()=>reader.f32()),t:Array.from({length:3},()=>reader.f32()),s:reader.f32()});
  transform();const count=reader.u32(),weighted=reader.u8(),bind=[];
  for(let i=0;i<count;i++){bind.push(transform());reader.p+=16;const nv=reader.u16();if(weighted)reader.p+=nv*6;}assert.equal(reader.p,reader.buf.length,'skin data consumed');
  const sp=nif.readSkinPartition(source.blocks[pi].raw),tris=sp.parts.flatMap(p=>p.trisCopy);
  const rawPositions=sp.verts.map((v,i)=>v.pos||s.dynamic?.[i]);assert(rawPositions.every(Boolean),'all vertices have positions');
  const normals=rawPositions.map(()=>[0,0,0]);for(const [a,b,c]of tris){const normal=cross(sub(rawPositions[b],rawPositions[a]),sub(rawPositions[c],rawPositions[a]));for(const i of [a,b,c])normals[i]=add(normals[i],normal);}
  const p=piece();for(let i=0;i<sp.verts.length;i++){
   const v=sp.verts[i],rawN=v.n?v.n.map(x=>x/255*2-1):norm(normals[i]);let pos=[0,0,0],n=[0,0,0],total=0;
   for(let k=0;k<4;k++){const w=nif.half(v.w[k]);if(w<=0)continue;const bone=bones[v.bi[k]],b=bind[v.bi[k]],world=pose.get(name(source.blocks[bone]))||rest(bone);
    assert(b&&world,'valid skin influence');const local=add(mul(apply(b.r,rawPositions[i]),b.s),b.t),at=add(mul(apply(world.r,local),world.s),world.t),normal=apply(world.r,apply(b.r,rawN));
    pos=add(pos,mul(at,w));n=add(n,mul(normal,w));total+=w;
   }assert(total>.98&&total<1.02,'normalized vertex weights');pos=mul(pos,1/total);n=norm(n);
   const shade=.42+.58*Math.max(0,dot(n,norm([-.4,.6,.7])));const alpha=Math.min(1,Math.max(0,pos[2]/12));
   p.verts.push(vertex(pos,n,alpha,shade,v.uv.map(nif.half)));
  }p.tris=tris;
  const shader=source.blocks[s.shader],texture=shader.textureSet>=0?source.blocks[shader.textureSet].textures[0]:shader.source;
  assert(texture,'stock diffuse available');result.push({label:name(s),p,texture});
 }return result;
}
function archer(female){
 const sex=female?'f':'m',head=female?'femalehead':'malehead';
 const paths=['cuirasslight_1.nif','gauntletslight_1.nif','bootslight_1.nif','helmetlight.nif'].map(f=>`meshes/armor/hide/${sex}/${f}`);
 paths.push(`meshes/actors/character/character assets/${head}.nif`);
 const files=paths.map(p=>prepare(load(p))),pose=poseFor(female?'female':'male'),result=files.flatMap(f=>shapes(f,pose));
 const bow=shapes(prepare(load('meshes/weapons/iron/ironbow.nif')),poseFor('bow'));
 const grip=pose.get('Shield');assert(grip,'left weapon attachment present');
 for(const shape of bow)for(const v of shape.p.verts){
  v.pos=add(grip.t,mul(apply(grip.r,v.pos),grip.s));
  const n=apply(grip.r,v.n.map(x=>x/255*2-1));v.n=n.map(x=>Math.round((x+1)*127.5));v.c[3]=255;
 }
 result.push(...bow);
 // The drawing animation positions Weapon at the arrow tip; the stock arrow's
 // shaft extends back to the right hand. Preserve the game's attachment transform.
 const arrowFile=prepare(load('meshes/weapons/iron/ironarrow.nif'));
 const ai=arrowFile.source.blocks.findIndex(b=>b.type==='BSTriShape'&&arrowFile.name(b)==='Arrow:0');
 assert(ai>=0,'single stock arrow present');
 const arrow=arrowFile.source.blocks[ai],local=arrowFile.rest(ai),attach=pose.get('Weapon'),p=piece();
 for(const v of arrow.verts){
  const q=add(local.t,mul(apply(local.r,v.pos),local.s)),pos=add(attach.t,mul(apply(attach.r,q),attach.s));
  const n=apply(attach.r,apply(local.r,v.n.map(x=>x/255*2-1)));
  p.verts.push(vertex(pos,n,1,.8,v.uv.map(nif.half)));
 }
 p.tris=arrow.tris;
 result.push({label:'NockedArrow',p,texture:arrowFile.source.blocks[arrowFile.source.blocks[arrow.shader].textureSet].textures[0]});
 // Body must be human-sized, with every fitted component connected in the common bind space.
 const vs=result.flatMap(s=>s.p.verts);const lo=[0,1,2].map(k=>Math.min(...vs.map(v=>v.pos[k]))),hi=[0,1,2].map(k=>Math.max(...vs.map(v=>v.pos[k])));
 assert(lo[2]>-20&&hi[2]>110&&hi[2]<180,'complete, correctly assembled human silhouette');
 console.log(`${female?'Female':'Male'} stock archer: ${result.length} shapes, bounds ${lo.map(x=>x.toFixed(1))} to ${hi.map(x=>x.toFixed(1))}`);
 return result;
}
module.exports={archer};
