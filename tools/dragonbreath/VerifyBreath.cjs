// Verify the shipped geometry against the actual C++ animation samples.
const fs=require('fs'),path=require('path'),assert=require('assert');
const {execFileSync}=require('child_process'),nifio=require('../wolfhead/nifio');
const repo=path.resolve(__dirname,'../..');
const look=JSON.parse(execFileSync(path.join(repo,'build/skyrim/Release/FightWithMeBreathLook.exe'),{maxBuffer:16*1024*1024}));
const read=stem=>nifio.read(fs.readFileSync(path.join(repo,'assets/meshes/FightWithMe',stem+'.nif')));
for(let rank=1;rank<=3;rank++) {
 const run=look.ranks[rank-1],nif=read(rank===3?'DragonBreathAlduin':'DragonBreathHead'),parents=new Map();
 nif.blocks.forEach((b,i)=>(b.children||[]).forEach(k=>parents.set(k,i)));
 let low=Infinity,openLow=Infinity,rear=Infinity;
 for(const f of run.frames) {
  if(f.a<.01)continue;
  for(let i=0;i<nif.blocks.length;i++) {
   const shape=nif.blocks[i];if(shape.type!=='BSTriShape')continue;
   const parent=nif.blocks[parents.get(i)],name=nif.strings[parent.name];
   if(!['Head','Jaw'].includes(name))continue;
   for(const v of shape.verts) {
    if(v.c[3]<10)continue;
    let [,y,z]=v.pos;
    if(name==='Jaw') {const c=Math.cos(f.jaw),s=Math.sin(f.jaw);[y,z]=[c*y-s*z+parent.translate[1],s*y+c*z+parent.translate[2]];}
    const c=Math.cos(f.pitch),s=Math.sin(f.pitch);[y,z]=[c*y-s*z,s*y+c*z];
    low=Math.min(low,f.height+f.s*z);rear=Math.min(rear,f.forward+f.s*y);
    if(f.t>=1&&f.t<=2)openLow=Math.min(openLow,f.height+f.s*z);
   }
  }
 }
 assert(low>=6&&low<40,`Rank ${rank} moving jaw clearance ${low}`);
 assert(openLow>=6&&openLow<=40,`Rank ${rank} open jaw clearance ${openLow}`);
 assert(rear>=15,`Rank ${rank} overlaps caster: ${rear}`);
 const suffix=['','II','III'][rank-1],frost=read('BreathFrost'+suffix),shock=read('BreathShock'+suffix);
 assert(nif.blocks.some(b=>nif.strings[b.name]==='SummoningRune'),'Missing summoning rune');
 const puffs=frost.blocks.filter(b=>b.type==='BSTriShape'&&/^Mist/.test(frost.strings[b.name]));
 assert.equal(puffs.length,32);
 for(const p of puffs){assert(p.verts.length>100);const rim=p.verts.slice(-25);assert(rim.every(v=>v.c[3]===0),'Frost edge must be fully transparent');}
 const names=new Set(shock.blocks.map(b=>shock.strings[b.name]));
 for(let i=0;i<64;i++){assert(names.has('Arc'+i));assert.notDeepEqual(run.frames[60].arcs[i],run.frames[65].arcs[i]);}
 for(const e of ['Fire','Frost','Shock']) {
  const geometry=read('Breath'+e+suffix);
  for(const shape of geometry.blocks.filter(b=>b.type==='BSTriShape'&&/^Flow/.test(geometry.strings[b.name])))assert(shape.verts.length>=1600,'Stream body must have round cross-sections rather than flat cards');
  if(e==='Fire')assert(!geometry.blocks.some(b=>b.source&&/FireballQuad/i.test(b.source)),'Fire atlas cells must not introduce square seams');
  const main=fs.readFileSync(path.join(repo,'assets/meshes/FightWithMe','Breath'+e+suffix+'.nif'));
  for(const copy of ['2','3','4'])assert(main.equals(fs.readFileSync(path.join(repo,'assets/meshes/FightWithMe','Breath'+e+suffix+copy+'.nif'))));
 }
 if(rank===3){for(let i=0;i<4;i++)assert(names.has('Ground'+i));for(let i=0;i<16;i++)assert(frost.blocks.some(b=>frost.strings[b.name]==='Ice'+i));}
 console.log(`PASS rank ${rank}: jaw ${low.toFixed(2)} above ground, rear ${rear.toFixed(2)} ahead, 32 soft frost puffs, 64 moving arcs, all instance copies match.`);
}
