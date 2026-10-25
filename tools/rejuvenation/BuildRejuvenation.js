// Textured seal and softly feathered magicka, posed by src/Rejuvenation.cpp.
// The seal artwork is generated, retained in art/; convert it with texconv before building.
const path = require('path');
const { piece, vertex, sprite, ribbon, meshWriter, FX, UP } = require('../fx/meshkit.js');
const OUT = path.join(__dirname, '../../assets/meshes/FightWithMe');
const SEAL = 'textures\\FightWithMe\\RejuvenationSeal.dds';
const m = meshWriter(), nodes = [];
function part(name, p, look) { nodes.push(m.node('NiNode', name, [m.shape(name+'Light', p, look)])); }
for (const name of ['Seal','GrandSeal']) {
  const p=piece(); sprite(p,560);
  part(name,p,{texture:SEAL,scale:1.45,soft:8});
}
let p=piece(); sprite(p,610); part('Bloom',p,{texture:FX.glowSpot,scale:0.65,soft:20});
for(let k=0;k<2;k++) {
  p=piece(); const segments=192, rows=18;
  for(let s=0;s<=segments;s++)for(let j=0;j<=rows;j++) {
    const a=s/segments*Math.PI*2, f=j/rows, envelope=Math.sin(Math.PI*f);
    const r=244-38*f+8*envelope*Math.sin(a*5+f*5+k*2);
    const z=f*(55+16*Math.sin(a*3+k))+5*envelope;
    const alpha=Math.pow(envelope,1.7)*(0.35+0.35*Math.sin(a*7+f*5+k)**2);
    p.verts.push(vertex([r*Math.cos(a),r*Math.sin(a),z],UP,alpha,1,[s/segments*5,f]));
  }
  for(let s=0;s<segments;s++)for(let j=0;j<rows;j++){
    const a=s*(rows+1)+j,b=a+rows+1; p.tris.push([a,b,a+1],[a+1,b,b+1]);
  }
  part('Mist'+k,p,{texture:FX.swirls,scale:0.85,soft:22});
}
p=piece();
for(let k=0;k<9;k++){
  const points=Array.from({length:56},(_,i)=>{const f=i/55,a=k*Math.PI*2/9+f*1.35,r=265-150*f;return [r*Math.cos(a),r*Math.sin(a),8+Math.sin(f*Math.PI)*(22+k%3*7)];});
  ribbon(p,points,f=>2+3*Math.sin(f*Math.PI),f=>Math.sin(Math.PI*f)**1.4,{vScale:70});
}
part('Filaments',p,{texture:FX.swirlStrip,scale:1.0,soft:10});
// Feathered horizontal ribbons float above the perimeter. No solid wall or hard rim.
for(let k=0;k<3;k++) {
  p=piece(); const segments=256, rows=10;
  for(let s=0;s<=segments;s++)for(let j=0;j<=rows;j++) {
    const a=s/segments*Math.PI*2, f=j/rows, feather=Math.sin(Math.PI*f)**2;
    const r=249+(f-.5)*34;
    const z=3.5*Math.sin(a*3+k)*feather;
    const alpha=feather*(0.62+0.38*Math.sin(a*5+k)**2);
    p.verts.push(vertex([r*Math.cos(a),r*Math.sin(a),z],UP,alpha,1,[s/segments*7,f]));
  }
  for(let s=0;s<segments;s++)for(let j=0;j<rows;j++) {
    const a=s*(rows+1)+j,b=a+rows+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);
  }
  part('EdgePulse'+k,p,{texture:FX.swirlStrip,scale:1.25,soft:16});
}
// Healing Circle's soft white heart, feathered radially into the blue seal.
p=piece();
const stops=[[0,1],[.2,.85],[.45,.55],[.7,.25],[1,0]], segments=128;
for(const [r,a] of stops)for(let s=0;s<=segments;s++) {
  const angle=s/segments*Math.PI*2;
  p.verts.push(vertex([170*r*Math.cos(angle),170*r*Math.sin(angle),0],UP,a));
}
for(let k=0;k<stops.length-1;k++)for(let s=0;s<segments;s++) {
  const i=k*(segments+1)+s,j=i+segments+1;
  p.tris.push([i,j,i+1],[i+1,j,j+1]);
}
part('Heart',p,{scale:.95,soft:8});
for(let i=0;i<72;i++){
  const name='Mote'+String(i).padStart(2,'0');
  const soft=piece();sprite(soft,12);
  const hot=piece();sprite(hot,3.6);
  const turn=m.billboard(name+'Turn',[m.shape(name+'Glow',soft,{texture:FX.glowSpot,scale:0.85}),m.shape(name+'Core',hot,{texture:FX.glowSoft,scale:1.65})]);
  nodes.push(m.node('NiNode',name,[turn]));
}
const root=m.node('BSFadeNode','RejuvenationCircle',nodes);
for(let i=0;i<4;i++)m.write(path.join(OUT,`RejuvenationCircle${i?i+1:''}.nif`),root);
