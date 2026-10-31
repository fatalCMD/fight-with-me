// Decode the saved SSE particle blocks independently of the builder.
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict');
const {read,Reader}=require('../wolfhead/nifio.js');
const root=path.resolve(__dirname,'../..');
function check(){
 const budgets={};
 for(const song of ['Fervor','Echoes','Hollow','Glory'])for(const kind of ['Aura','Wave','Hit'])for(let rank=1;rank<=3;rank++){
  const name=`Skald${song}${kind}${rank}`,n=read(fs.readFileSync(path.join(root,'assets/meshes/FightWithMe',name+'.nif')));
  assert.equal(n.bsver,100);assert.equal(n.user,12);assert(!n.types.includes('BSTriShape'));assert(!n.types.includes('NiControllerManager'));
  const ref=(i,type)=>{assert(i>=0&&i<n.blocks.length,`${name}: dangling reference ${i}`);const b=n.blocks[i];if(type)assert.equal(b.type,type);return b;};
  const string=i=>{assert(i>=0&&i<n.strings.length);return n.strings[i];};
  const full=(r,b)=>assert.equal(r.p,b.raw.length,`${name}: ${b.type} byte layout`);
  const floats=(r,count)=>Array.from({length:count},()=>{const x=r.f32();assert(Number.isFinite(x));return x;});
  const systems=n.blocks.filter(b=>b.type==='NiParticleSystem');assert.equal(systems.length,rank===3?4:3);
  let peak=0;
  for(const b of systems){
   const system=n.blocks.indexOf(b),r=new Reader(b.raw);string(r.i32());assert.deepEqual(r.refs(),[]);const emitterCtl=r.i32();r.u32();floats(r,13);assert.equal(r.i32(),-1);const bound=floats(r,4);assert(bound[3]>0);assert.equal(r.i32(),-1);
   const shader=ref(r.i32(),'BSEffectShaderProperty'),alpha=ref(r.i32(),'NiAlphaProperty');r.u64();for(let i=0;i<4;i++)r.u16();
   const data=ref(r.i32(),'NiPSysData');assert.equal(r.u8(),1,'Particle trails must live in world space');const mods=r.refs();full(r,b);
   assert.equal(alpha.alphaFlags,237);assert(shader.flags1&0x40000000);assert(shader.flags1&0x80000000);assert(!(shader.flags2&1));assert(shader.color.some(x=>x>0));
   assert(mods.some(i=>ref(i).type==='NiPSysAgeDeathModifier'));assert(mods.some(i=>ref(i).type==='NiPSysPositionModifier'));assert(mods.some(i=>ref(i).type==='NiPSysBoundUpdateModifier'));
   const cap=data.raw.readUInt16LE(4),atlas=data.raw.readUInt32LE(47);assert.equal(data.raw.readInt32LE(35),-1);assert.equal(data.raw.length,70+16*atlas);assert.equal(cap,512);
   const atlasCounts={'FXsmokePuffAnim04.dds':64,'FXGlowSpotLinearAlpha.dds':0,'MagicIceWisps.dds':4,'FXSmokeNecroAnim.dds':128};
   const texture=shader.source.split('\\').at(-1);assert.equal(atlas,atlasCounts[texture]);assert.equal(mods.some(i=>ref(i).type==='BSPSysSubTexModifier'),atlas>=64);
   assert(mods.some(i=>ref(i).type==='NiPSysGravityModifier'),'Native drift/turbulence must be present');
   let life=0,variation=0,emitterName;
   for(const id of mods){
    const m=ref(id),s=new Reader(m.raw),label=string(s.i32());s.u32();assert.equal(s.i32(),system);assert.equal(s.u8(),1);
    if(m.type==='NiPSysCylinderEmitter'){
     const velocity=floats(s,6);assert(velocity[0]>=0&&velocity[1]<=velocity[0]);floats(s,4);const size=floats(s,2);assert(size[0]>size[1]);[life,variation]=floats(s,2);assert(life>variation&&life<2);ref(s.i32(),'NiNode');floats(s,2);emitterName=label;full(s,m);
    }else if(m.type==='BSPSysSimpleColorModifier'){
     const transition=floats(s,6),colors=floats(s,12);assert(transition.every(x=>x>=0&&x<=1));assert.equal(colors[3],0);assert(colors[7]>0&&colors[7]<=1);assert.equal(colors[11],0);full(s,m);
    }
   }
   const c=ref(emitterCtl,'NiPSysEmitterCtlr'),s=new Reader(c.raw),next=s.i32(),flags=s.u16();assert.equal(flags&1,1,'Bursts must use application-initialized time');assert.equal(flags&6,4,'One-shot emitters must clamp, not loop');assert(!(flags&32),'No external manager');assert.equal(s.f32(),1);assert.equal(s.f32(),0);assert.equal(s.f32(),0);const stop=s.f32();assert.equal(s.i32(),system);
   const ip=ref(s.i32(),'NiFloatInterpolator');assert.equal(string(s.i32()),emitterName);const visible=ref(s.i32(),'NiBoolInterpolator');assert.equal(visible.raw[0],1);full(s,c);
   const update=ref(next,'NiPSysUpdateCtlr');assert.equal(update.raw.readInt32LE(22),system);assert.equal(update.raw.readInt32LE(0),-1);
   assert.equal(update.raw.readUInt16LE(4)&1,1);assert.equal(update.raw.readFloatLE(14),0);assert.equal(update.raw.readFloatLE(18),0);
   const keys=ref(ip.raw.readInt32LE(4),'NiFloatData'),k=new Reader(keys.raw),count=k.u32();assert.equal(k.u32(),1);let prior=-1,total=0,lastRate=0,emissionEnd=0;
   for(let i=0;i<count;i++){const time=k.f32(),rate=k.f32();assert(time>prior&&rate>=0&&Number.isFinite(rate));if(i)total+=(time-prior)*(rate+lastRate)*.5;if(rate>0)emissionEnd=time;prior=time;lastRate=rate;}full(k,keys);
   assert.equal(lastRate,0);assert.equal(prior,stop);assert(stop>=emissionEnd+life+variation,'Effect must outlive the last particle');assert(total<cap);peak+=total;
  }
  (budgets[song+kind]??=[]).push(peak);console.log(`${name}: native graph valid, approximately ${Math.ceil(peak)} particles per burst`);
 }
 for(const counts of Object.values(budgets))assert(counts[0]<counts[1]&&counts[1]<counts[2]);
 console.log('Native Ljodhs: 36 SSE graphs, lifetime tails, controller links, atlas bounds and rank budgets passed.');
}
if(require.main===module)check();module.exports={check};
