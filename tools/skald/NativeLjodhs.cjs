// SSE native emitters. Each instance is a short, independently fading burst.
// Schema: niftools/nifxml; particle-data layouts/atlas come from the user's stock BSAs.
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict');
const nif=require('../wolfhead/nifio.js'),{open}=require('../wolfhead/bsa.js');
const root=path.resolve(__dirname,'../..'),identity=[1,0,0,0,1,0,0,0,1];
const profiles=[
 {song:'Fervor',mist:[.64,.19,.035],light:[1,.65,.17],accent:[1,.36,.055],texture:'FXGlowSpotLinearAlpha.dds',speed:62,spin:.9,force:14,turbulence:12,power:2.1,label:'Rising embers / golden streaks / fervent corona'},
 {song:'Echoes',mist:[.19,.51,.60],light:[.57,.88,1],accent:[.27,.77,1],texture:'MagicIceWisps.dds',speed:18,spin:.24,force:3,turbulence:8,power:1.55,label:'Harmonic filaments / pearl motes / spectral mantle'},
 {song:'Hollow',mist:[.14,.035,.23],light:[.65,.25,1],accent:[.51,.13,.95],texture:'FXSmokeNecroAnim.dds',speed:12,spin:-.34,force:-7,turbulence:7,power:1.7,label:'Violet tendrils / sinking cinders / shadow mantle'},
 {song:'Glory',mist:[.46,.035,.10],light:[1,.76,.25],accent:[1,.55,.11],texture:'FXGlowSpotLinearAlpha.dds',speed:35,spin:.14,force:7,turbulence:5,power:2.3,label:'Gold comets / crimson veil / radiant mantle'}
];
const templatesByTexture={
 'FXsmokePuffAnim04.dds':['effects\\fxdustdrop01.nif',0],
 'FXGlowSpotLinearAlpha.dds':['effects\\fxcameraattachfinedust.nif',0],
 'MagicIceWisps.dds':['magic\\soultraptargeteffects.nif',0],
 'SparksGrayGlow.dds':['magic\\healritualcastbodyfx.nif',0],
 'FXSmokeNecroAnim.dds':['dlc01\\effects\\dlc1soulcairnshadefxmulti.nif',0]
};
const specs=[];
for(const [songIndex,profile]of profiles.entries())for(let rank=1;rank<=3;rank++)for(const kind of ['Aura','Wave','Hit']){
 const wave=kind==='Wave',hit=kind==='Hit',density=[0,1,1.65,3.1][rank],hollow=songIndex===2;
 const common={emit:wave?.16:hit?.16:.25,life:wave?1.45:1.05,lifeVariation:.13,
  speed:wave?760:hit?26:18,speedVariation:wave?100:7,declination:wave?Math.PI/2:.15,spread:wave?.015:.55,
  radius:wave?40:hit?(rank===3?32:23):34,height:wave?8:hit?[0,14,40,86][rank]:rank===3?100:65,z:wave?14:hit?[0,10,24,48][rank]:rank===3?70:56,
  angleVariation:Math.PI,spin:profile.spin,force:wave?2:profile.force,turbulence:profile.turbulence,aspect:1,power:1,frames:52};
 // Normal alpha cannot add light, so on a bright floor visibility comes from
 // size, opacity and an emissive scale above 1, never from more particles.
 const glow=1.3;
 const layers=[
  {...common,name:'Veil',texture:'FXsmokePuffAnim04.dds',animate:true,rate:(wave?190:hit?65:50)*density,size:wave?40:hit?22:26,sizeVariation:wave?14:7,
   opacity:wave?.26:hit?.3:.24,color:profile.mist,growth:[.45,1,1.5,1.9]},
  {...common,name:'Motes',texture:'FXGlowSpotLinearAlpha.dds',animate:false,rate:(wave?150:hit?75:45)*density,size:wave?7:5.5,sizeVariation:1.8,
   speed:wave?800:profile.speed+15,opacity:.95,color:profile.light,power:profile.power*glow,growth:[.4,1,.8,.2]},
  {...common,name:'Signature',texture:profile.texture,animate:hollow,rate:(wave?130:hit?32:27)*density,
   size:wave?(hollow?32:23):(hollow?32:songIndex===1?32:17),sizeVariation:5,speed:wave?750:profile.speed,
   speedVariation:wave?100:Math.min(7,profile.speed),declination:wave?1.53:hollow?Math.PI-.4:.4,spread:wave?.03:.35,
   opacity:hollow?.85:.92,color:profile.accent,power:profile.power*glow,aspect:songIndex===0?.38:songIndex===3?.24:1,
   angleVariation:songIndex===0||songIndex===3?.18:Math.PI,frames:hollow?76:0,growth:[.35,1,1.25,.35]}
 ];
 // Rank III adds a new upper-body layer; it is not just a brightness multiplier.
 if(rank===3)layers.push({...common,name:'Ascendant',texture:songIndex===2?'FXSmokeNecroAnim.dds':'MagicIceWisps.dds',animate:hollow,
  rate:wave?55:hit?58:42,size:wave?40:44,sizeVariation:9,radius:wave?70:43,height:wave?24:104,z:wave?26:76,
  speed:wave?740:hollow?8:22,speedVariation:wave?95:6,declination:wave?1.5:hollow?Math.PI-.2:.4,
  opacity:hollow?.7:.68,color:songIndex===3?[.88,.045,.16]:profile.accent,power:2,frames:hollow?66:0,growth:[.2,.8,1.4,.7]});
 specs.push({name:`Skald${profile.song}${kind}${rank}`,song:profile.song,songIndex,kind,rank,seconds:wave?1.95:1.65,layers});
}
function templates(){
 const out={};
 for(const archive of ['Skyrim - Meshes0.bsa','Skyrim - Meshes1.bsa']){
  const bsa=open(path.join(root,'../STOCK GAME/Data',archive));
  for(const [texture,[name,index]]of Object.entries(templatesByTexture))if(!out[texture]){
   const file=bsa.files.find(f=>f.path.toLowerCase()==='meshes\\'+name);
   if(file){const n=nif.read(bsa.read(file));out[texture]=Buffer.from(n.blocks.filter(b=>b.type==='NiPSysData')[index].raw);}
  }
  if(Object.keys(out).length===Object.keys(templatesByTexture).length)break;
 }
 assert.equal(Object.keys(out).length,Object.keys(templatesByTexture).length,'Missing native particle templates');
 for(const b of Object.values(out)){assert.equal(b.readInt32LE(35),-1);b.writeUInt16LE(512,4);}
 for(const spec of specs)for(const p of spec.layers){const data=out[p.texture];p.atlasCount=data.readUInt32LE(47);p.columns=p.atlasCount?Math.round(1/data.readFloatLE(55)):1;p.rows=p.atlasCount?Math.round(1/data.readFloatLE(63)):1;}
 return out;
}
function make(spec,dataTemplates){
 const strings=[],blocks=[];const str=s=>{let i=strings.indexOf(s);if(i<0){i=strings.length;strings.push(s);}return i;};
 const add=b=>(blocks.push(b)-1),raw=(type,fn)=>{const w=new nif.Writer();fn(w);return add({type,raw:w.done()});};
 const av=(w,name,controller,at=[0,0,0])=>{w.i32(str(name));w.refs([]);w.i32(controller);w.u32(0xc000e);at.forEach(x=>w.f32(x));identity.forEach(x=>w.f32(x));w.f32(1);w.i32(-1);};
 const node=(name,at=[0,0,0])=>add({type:'NiNode',name:str(name),extra:[],controller:-1,flags:0xc000e,translate:at,rotate:identity,scale:1,collision:-1,children:[],effects:[]});
 const parent=node(spec.song+'Native');blocks[parent].extra=[raw('BSXFlags',w=>{w.i32(str('BSX'));w.u32(1);})];
 for(const layer of spec.layers){
  const emitterNode=node(layer.name+'Origin',[0,0,layer.z]);blocks[parent].children.push(emitterNode);
  const particleData=Buffer.from(dataTemplates[layer.texture]);particleData.writeFloatLE(layer.aspect,51+16*layer.atlasCount);
  const data=add({type:'NiPSysData',raw:particleData});
  const shader=add({type:'BSEffectShaderProperty',name:-1,extra:[],controller:-1,
   flags1:0xc0000008,flags2:0x20,uvOffset:[0,0],uvScale:[1,1],
   source:'textures\\effects\\'+layer.texture,
   clamp:0,lighting:0,envMinLod:0,unused:0,falloff:[1,0,1,1],color:[1,1,1,1],colorScale:layer.power,softDepth:18,greyscale:''});
  // Normal alpha avoids turning every visible mote into an ENB additive light source.
  const alpha=add({type:'NiAlphaProperty',name:-1,extra:[],controller:-1,alphaFlags:237,threshold:0});
  const system=add({type:'NiParticleSystem'}),mods=[];
  const modifier=(type,name,order,fn)=>{const id=raw(type,w=>{w.i32(str(name));w.u32(order);w.i32(system);w.u8(1);fn(w);});mods.push(id);return id;};
  modifier('NiPSysAgeDeathModifier','Age',0,w=>{w.u8(0);w.i32(-1);});
  modifier('NiPSysCylinderEmitter','Emit',1000,w=>{
   [layer.speed,layer.speedVariation,layer.declination,layer.spread,0,Math.PI].forEach(x=>w.f32(x));
   [1,1,1,1,layer.size,layer.sizeVariation,layer.life,layer.lifeVariation].forEach(x=>w.f32(x));
   w.i32(emitterNode);w.f32(layer.radius);w.f32(layer.height);
  });
  modifier('BSPSysSimpleColorModifier','Color',3000,w=>{
   [.12,.6,.2,0,.6,1].forEach(x=>w.f32(x));
   for(const color of [[...layer.color,0],[...layer.color,layer.opacity],[...layer.color,0]])color.forEach(x=>w.f32(x));
  });
  modifier('BSPSysScaleModifier','Scale',3000,w=>{w.u32(16);for(let i=0;i<16;i++){const t=i/15*3,k=Math.min(2,Math.floor(t));w.f32(layer.growth[k]+(layer.growth[k+1]-layer.growth[k])*(t-k));}});
  modifier('NiPSysRotationModifier','Rotate',3000,w=>{[layer.spin,.12,0,layer.angleVariation].forEach(x=>w.f32(x));w.u8(1);w.u8(0);[0,0,1].forEach(x=>w.f32(x));});
  if(layer.animate)modifier('BSPSysSubTexModifier','AnimateVapor',3000,w=>[0,6,layer.atlasCount-1,layer.atlasCount-1,0,layer.frames,4].forEach(x=>w.f32(x)));
  modifier('NiPSysGravityModifier','Drift',4000,w=>{w.i32(emitterNode);[0,0,1,0,layer.force].forEach(x=>w.f32(x));w.u32(0);w.f32(layer.turbulence);w.f32(.4);w.u8(1);});
  modifier('NiPSysPositionModifier','Move',6000,()=>{});
  modifier('NiPSysBoundUpdateModifier','Bounds',7000,w=>w.u16(1));
  const keys=[[0,0],[.025,layer.rate],[layer.emit*.75,layer.rate],[layer.emit,0],[spec.seconds,0]];
  const rateData=raw('NiFloatData',w=>{w.u32(keys.length);w.u32(1);for(const [time,value]of keys){w.f32(time);w.f32(value);}});
  const rate=raw('NiFloatInterpolator',w=>{w.f32(0);w.i32(rateData);});
  const on=raw('NiBoolInterpolator',w=>{w.u8(1);w.i32(-1);});
  // APP_INIT: each clone begins when StartAnimations is requested, rather than
  // inheriting the scene's already-running application clock. The simulation
  // controller uses vanilla's zero key range; only emission has a keyed tail.
  const clock=(w,next,stop=spec.seconds)=>{w.i32(next);w.u16(77);w.f32(1);w.f32(0);w.f32(0);w.f32(stop);w.i32(system);};
  const update=raw('NiPSysUpdateCtlr',w=>clock(w,-1,0));
  const emit=raw('NiPSysEmitterCtlr',w=>{clock(w,update);w.i32(rate);w.i32(str('Emit'));w.i32(on);});
  const w=new nif.Writer();av(w,layer.name,emit);
  [0,0,0,spec.kind==='Wave'?1600:220].forEach(x=>w.f32(x));w.i32(-1);w.i32(shader);w.i32(alpha);
  w.u64(0x0840200004000051n);[0,0,0,0].forEach(x=>w.u16(x));w.i32(data);w.u8(1);w.refs(mods);
  blocks[system].raw=w.done();blocks[parent].children.push(system);
 }
 return {strings,blocks,roots:[parent],groups:[],author:'Fight With Me',process:'Skald native particle effects'};
}
function build(){
 const data=templates(),out=path.join(root,'assets/meshes/FightWithMe');
 for(const spec of specs)fs.writeFileSync(path.join(out,spec.name+'.nif'),nif.write(make(spec,data)));
 fs.writeFileSync(path.join(__dirname,'native-ljodhs.json'),JSON.stringify({version:2,profiles,specs},null,2)+'\n');
 console.log('Four Ljodhs: 36 native burst models; three particle layers, plus an upper aura at rank III.');
}
if(require.main===module)build();
module.exports={build,specs,profiles};
