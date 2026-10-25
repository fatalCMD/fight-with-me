// Original arrow-sized elemental components, using only Skyrim particle textures.
const path = require('path');
const {piece,ribbon,sprite,meshWriter,FX,random} = require('../fx/meshkit');
for (const [element, name] of ['Fire','Frost','Shock'].entries()) {
  const m=meshWriter(), rand=random(804+element), core=piece(), veil=piece();
  for(let k=0;k<(element===2?7:5);k++) {
    const angle=k*2.399, points=[];
    for(let i=0;i<=24;i++) {
      const f=i/24, radius=(2+f*5), twist=angle+f*5;
      const jitter=element===2?(rand()-.5)*8:0;
      points.push([Math.cos(twist)*radius+jitter,20-f*78,Math.sin(twist)*radius+jitter*.6]);
    }
    ribbon(core,points,element===2?4:element===0?6:5,f=>Math.sin(Math.PI*f)**.65*.8,{vScale:element===0?35:65});
  }
  // Two crossing soft sheets avoid a hard ribbon silhouette from the side.
  for(let k=0;k<3;k++) {
    const angle=k*Math.PI/3;
    const p=piece();ribbon(p,Array.from({length:25},(_,i)=>[Math.sin(i*.3)*2,12-i*3.8,0]),element===1?20:12,f=>Math.sin(Math.PI*f)*.45,{vScale:60});
    for(const v of p.verts){const x=v.pos[0],z=v.pos[2];v.pos[0]=x*Math.cos(angle)-z*Math.sin(angle);v.pos[2]=x*Math.sin(angle)+z*Math.cos(angle);}
    const offset=veil.verts.length;veil.verts.push(...p.verts);veil.tris.push(...p.tris.map(t=>t.map(i=>i+offset)));
  }
  const nodes=[m.node('NiNode','Filaments',[m.shape('ElementThreads',core,{scale:element===0?1.5:element===1?3:5,soft:2,texture:element===0?FX.fireTile:element===1?FX.frostCloud:FX.bolt})]),
    m.node('NiNode','Veil',[m.shape('ElementMist',veil,{scale:element===1?2:1,soft:3,texture:element===0?FX.fireWave:element===1?FX.frostCloud:FX.glowSoft})])];
  for(let i=0;i<12;i++) {
    const p=piece();sprite(p,element===1?4.5:element===0?1.5:2,{alpha:.65,cols:element===1?4:1,rows:element===1?4:1,cell:i});
    nodes.push(m.node('NiNode','Mote'+i,[m.billboard('Facing'+i,[m.shape('Particle'+i,p,{scale:element===0?1.2:3,soft:1.5,texture:element===1?FX.iceShards:FX.spark})])]));
  }
  m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/SpellArrow'+name+'.nif'),m.node('BSFadeNode','SpellArrow'+name,[m.node('NiNode','Trail',nodes)]));
}
