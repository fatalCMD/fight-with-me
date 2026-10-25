// Original spectral fortress geometry. The masonry uses the player's installed Skyrim texture.
const fs = require('fs'), path = require('path');
const { piece, vertex, ribbon, band, wall, sprite, meshWriter, random, FX } = require('../fx/meshkit.js');
const OUT = path.resolve(__dirname, '../../assets');
const STONE = 'textures\\architecture\\solitude\\sstonewall02.dds';
const PALETTE = 'textures\\FightWithMe\\BastionStoneGradient.dds';
const CRIMSON = 'textures\\FightWithMe\\BastionCrimsonGradient.dds';
const tau = Math.PI * 2, identity = [1,0,0,0,1,0,0,0,1];
function face(p, points, normal, bright=1) {
  const start=p.verts.length;
  const uvs=[[0,0],[1,0],[1,1],[0,1]];
  points.forEach((v,i)=>p.verts.push(vertex(v,normal,1,bright,uvs[i])));
  p.tris.push([start,start+1,start+2],[start,start+2,start+3]);
}
function box(p, centre, width, depth, height, angle=0, bright=1) {
  const c=Math.cos(angle),s=Math.sin(angle), at=([x,y,z])=>[centre[0]+x*c-y*s,centre[1]+x*s+y*c,centre[2]+z];
  const turn=([x,y,z])=>[x*c-y*s,x*s+y*c,z];
  const x=width/2,y=depth/2,z=height/2;
  const faces=[[[[-x,-y,-z],[x,-y,-z],[x,-y,z],[-x,-y,z]],[0,-1,0]],
    [[[x,y,-z],[-x,y,-z],[-x,y,z],[x,y,z]],[0,1,0]],
    [[[-x,y,-z],[-x,-y,-z],[-x,-y,z],[-x,y,z]],[-1,0,0]],
    [[[x,-y,-z],[x,y,-z],[x,y,z],[x,-y,z]],[1,0,0]],
    [[[-x,-y,z],[x,-y,z],[x,y,z],[-x,y,z]],[0,0,1]],
    [[[-x,y,-z],[x,y,-z],[x,-y,-z],[-x,-y,-z]],[0,0,-1]]];
  for(const [pts,n] of faces)face(p,pts.map(at),turn(n),bright);
}
function shell(p,c,r,thick,low,high,{gate=false,segments=128,bright=1}={}) {
  const point=(a,rad,z)=>[c[0]+Math.cos(a)*rad,c[1]+Math.sin(a)*rad,z];
  const bottom=a=>{const d=Math.atan2(Math.sin(a-Math.PI/2),Math.cos(a-Math.PI/2));return gate&&Math.abs(d)<.31?Math.max(low,59+22*Math.sqrt(Math.max(0,1-(d/.31)**2))):low;};
  for(let i=0;i<segments;i++) {
    const a=tau*i/segments,b=tau*(i+1)/segments,mid=(a+b)/2;
    const za=Math.min(bottom(a),high),zb=Math.min(bottom(b),high);
    if(za===high&&zb===high)continue;
    for(const [rad,sign] of [[r,1],[r-thick,-1]]) {
      const start=p.verts.length;
      for(const [ang,z,u,v] of [[a,za,i/segments*6,za/65],[b,zb,(i+1)/segments*6,zb/65],[b,high,(i+1)/segments*6,high/65],[a,high,i/segments*6,high/65]])
        p.verts.push(vertex(point(ang,rad,z),[Math.cos(ang)*sign,Math.sin(ang)*sign,0],1,bright,[u,v]));
      p.tris.push([start,start+1,start+2],[start,start+2,start+3]);
    }
    face(p,[point(a,r,high),point(b,r,high),point(b,r-thick,high),point(a,r-thick,high)],[0,0,1],bright);
  }
}
function hoop(p,c,r,z,width=1.5,alpha=.65) {
  ribbon(p,Array.from({length:129},(_,i)=>[c[0]+r*Math.cos(i*tau/128),c[1]+r*Math.sin(i*tau/128),z]),width,alpha);
}
function tower(stone,edge,c,r,h,gate=false) {
  shell(stone,c,r,9,5,h,{gate});
  for(const [z,radius,tall] of [[4,r+9,8],[16,r+4,5],[h*.48,r+2,4],[h-12,r+4,5],[h,r+7,8]])
    shell(stone,c,radius,12,z,z+tall,{gate:gate&&z<82,bright:.92});
  const teeth=Math.max(10,Math.round(r/5.5));
  for(let i=0;i<teeth;i++) {
    const a=i*tau/teeth;
    box(stone,[c[0]+Math.cos(a)*(r-1),c[1]+Math.sin(a)*(r-1),h+17],r*tau/teeth*.57,14,26,a+Math.PI/2,.98);
    box(stone,[c[0]+Math.cos(a)*(r-1),c[1]+Math.sin(a)*(r-1),h+31],r*tau/teeth*.65,16,3,a+Math.PI/2,.88);
  }
  for(let i=0;i<8;i++) {
    const a=i*tau/8;
    if(gate&&i===2)continue;
    const radius=r+2;
    box(stone,[c[0]+Math.cos(a)*radius,c[1]+Math.sin(a)*radius,h*.43],7,9,h*.72,a+Math.PI/2,.8);
    // Recess-like arrow slits with a luminous narrow centre.
    const z=h*.68;
    box(stone,[c[0]+Math.cos(a)*(r+.8),c[1]+Math.sin(a)*(r+.8),z],6,2,25,a+Math.PI/2,.28);
    ribbon(edge,[[c[0]+Math.cos(a)*(r+2),c[1]+Math.sin(a)*(r+2),z-10],[c[0]+Math.cos(a)*(r+2),c[1]+Math.sin(a)*(r+2),z+10]],1,.6);
  }
  hoop(edge,c,r+7,h+5,1.6,.6); hoop(edge,c,r+8,10,1.5,.4);
  if(gate) {
    const y=c[1]+r+1;
    for(const x of [-25,25])box(stone,[c[0]+x,y,31],7,12,62,0,.88);
    for(let i=0;i<15;i++) {
      const a=Math.PI*i/15,b=Math.PI*(i+1)/15;
      // Radial arch blocks, with real thickness and a visible keystone.
      const pts=[[22*Math.cos(a),22*Math.sin(a)],[29*Math.cos(a),29*Math.sin(a)],[29*Math.cos(b),29*Math.sin(b)],[22*Math.cos(b),22*Math.sin(b)]];
      const front=pts.map(([x,z])=>[c[0]+x,y+5,59+z]),back=pts.map(([x,z])=>[c[0]+x,y-6,59+z]);
      face(stone,front,[0,1,0],i%2?.9:1);face(stone,back,[0,-1,0],.8);
      for(let k=0;k<4;k++)face(stone,[front[k],back[k],back[(k+1)%4],front[(k+1)%4]],[0,0,1],.9);
    }
    const arch=Array.from({length:40},(_,i)=>{const a=Math.PI*i/39;return [c[0]+23*Math.cos(a),y+6,59+23*Math.sin(a)];});
    ribbon(edge,[[c[0]+23,y+6,3],...arch,[c[0]-23,y+6,3]],1.8,.7);
  }
}
function build(grand) {
  const m=meshWriter(),stone=piece(),edge=piece(),nodes=[];
  if(grand) {
    tower(stone,edge,[0,0],76,230,true);
    tower(stone,edge,[0,0],139,83,true);
    for(let i=0;i<6;i++){const a=i*tau/6; tower(stone,edge,[Math.cos(a)*128,Math.sin(a)*128],29,i%2?151:176,false);}
    // Six buttresses and stepped upper keep lend the higher rank a different silhouette.
    for(let i=0;i<6;i++){const a=i*tau/6;box(stone,[Math.cos(a)*83,Math.sin(a)*83,105],13,23,205,a+Math.PI/2,.84);}
  } else tower(stone,edge,[0,0],73,162,true);
  function node(name,p,look) {
    const chunks=[];let chunk=piece(),map=new Map();
    for(const tri of p.tris) {
      const [a,b,c]=tri.map(i=>p.verts[i].pos),u=b.map((v,k)=>v-a[k]),v=c.map((x,k)=>x-a[k]);
      if(Math.hypot(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0])<1e-7)continue;
      if(chunk.verts.length>49990){chunks.push(chunk);chunk=piece();map=new Map();}
      chunk.tris.push(tri.map(i=>{if(!map.has(i)){map.set(i,chunk.verts.length);chunk.verts.push(p.verts[i]);}return map.get(i);}));
    }
    if(chunk.tris.length)chunks.push(chunk);
    const shapes=chunks.map((part,i)=>m.shape(name+'Geometry'+i,part,look));
    nodes.push(m.addBlock({type:'NiNode',name:m.str(name),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:identity,scale:.0001,collision:-1,children:shapes,effects:[]}));
  }
  // Skyrim's palette lookup replaces BaseColor.rgb; a runtime red tint cannot colour gray stone.
  node('Stone',stone,{scale:grand?1.15:1.5,normal:true,texture:STONE,palette:grand?CRIMSON:PALETTE,soft:3});
  node('Tracery',edge,{scale:1.4,texture:FX.glowSpot,soft:8});
  const impact=piece();band(impact,76,110,2,[0,.55,1,.2,0],160);
  node('Impact',impact,{scale:1.8,texture:FX.frostCloud,soft:12});
  const dust=piece();wall(dust,90,40,[0,.6,.38,0],128,5);band(dust,65,130,3,[0,.5,.35,0],128);
  node('Dust',dust,{scale:1.3,texture:FX.frostCloud,soft:20});
  const ward=piece(),wedge=piece();
  for(let k=0;k<3;k++) {
    const a=k*tau/3+.2, rows=12,columns=18,base=ward.verts.length;
    for(let j=0;j<=rows;j++)for(let i=0;i<=columns;i++) {
      const u=i/columns,v=j/rows,t=a+(u-.5)*.76,z=22+80*v+12*Math.abs(u-.5)*(1-v),r=40+3*Math.sin(v*Math.PI);
      ward.verts.push(vertex([Math.cos(t)*r,Math.sin(t)*r,z],[Math.cos(t),Math.sin(t),0],Math.sin(Math.PI*u)*Math.sin(Math.PI*v),1,[u,v]));
    }
    for(let j=0;j<rows;j++)for(let i=0;i<columns;i++){const n=base+j*(columns+1)+i;ward.tris.push([n,n+1,n+columns+1],[n+1,n+columns+2,n+columns+1]);}
    for(const side of [-1,1])ribbon(wedge,Array.from({length:18},(_,i)=>{const v=i/17,t=a+side*.38,r=40+3*Math.sin(v*Math.PI);return [Math.cos(t)*r,Math.sin(t)*r,28+74*v];}),3.2,f=>.85*Math.sin(Math.PI*f));
    const crest=Array.from({length:19},(_,i)=>{const t=a+(i/18-.5)*.76;return[Math.cos(t)*42,Math.sin(t)*42,101+2*Math.cos(i/18*Math.PI*2)];});
    ribbon(wedge,crest,3.2,.85);
    // Small three-merlon fortress crest, conforming to the shield surface.
    const xy=[[-.17,62],[-.17,78],[-.1,78],[-.1,72],[-.035,72],[-.035,81],[.035,81],[.035,72],[.1,72],[.1,78],[.17,78],[.17,62],[0,51],[-.17,62]];
    ribbon(wedge,xy.map(([u,z])=>[Math.cos(a+u)*44,Math.sin(a+u)*44,z]),3.2,.9);
  }
  node('Ward',ward,{scale:1.1,texture:FX.frostCloud,normal:true,soft:12});
  node('WardGlow',wedge,{scale:3.4,soft:8});
  const motes=[],rand=random(41928);
  for(let i=0;i<(grand?44:26);i++) {
    const p=piece();sprite(p,.7+rand()*.85,{alpha:.5+rand()*.4});
    const s=m.shape('WardMote'+i,p,{scale:1.2,texture:FX.glowSpot,soft:4}),b=m.billboard('WardMoteFacing'+i,[s]);
    const angle=rand()*tau,r=38+rand()*18;
    motes.push(m.addBlock({type:'NiNode',name:m.str('WardMoteAt'+i),extra:[],controller:-1,flags:0x8000e,translate:[Math.cos(angle)*r,Math.sin(angle)*r,12+rand()*108],rotate:identity,scale:1,collision:-1,children:[b],effects:[]}));
  }
  nodes.push(m.addBlock({type:'NiNode',name:m.str('Sparks'),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:identity,scale:.0001,collision:-1,children:motes,effects:[]}));
  const root=m.node('BSFadeNode','LastBastion',nodes);
  m.write(path.join(OUT,'meshes/FightWithMe',grand?'LastBastionCastle.nif':'LastBastionTower.nif'),root);
}
const h=Buffer.alloc(128),w=256;
h.write('DDS ',0);h.writeUInt32LE(124,4);h.writeUInt32LE(0x100f,8);h.writeUInt32LE(4,12);h.writeUInt32LE(w,16);h.writeUInt32LE(w*4,20);
h.writeUInt32LE(32,76);h.writeUInt32LE(0x41,80);h.writeUInt32LE(32,88);h.writeUInt32LE(0xff0000,92);h.writeUInt32LE(0xff00,96);h.writeUInt32LE(0xff,100);h.writeUInt32LE(0xff000000,104);h.writeUInt32LE(0x1000,108);
const pixels=Buffer.alloc(w*4*4);
for(let y=0;y<4;y++)for(let x=0;x<w;x++){const c=Math.round(40+185*Math.pow(x/255,.46)),i=(y*w+x)*4;pixels[i]=pixels[i+1]=pixels[i+2]=c;pixels[i+3]=255;}
fs.mkdirSync(path.join(OUT,'textures/FightWithMe'),{recursive:true});fs.writeFileSync(path.join(OUT,PALETTE.replace(/\\/g,'/')),Buffer.concat([h,pixels]));
// Every row stays crimson: BaseColor.r is the palette's vertical lookup coordinate.
// Keep the brightest masonry below unit emission, including its material gain.
const crimson=Buffer.alloc(w*4*4);
for(let y=0;y<4;y++)for(let x=0;x<w;x++){
  const light=Math.pow(x/255,.46),i=(y*w+x)*4;
  crimson[i]=Math.round(2+12*light);
  crimson[i+1]=Math.round(1+8*light);
  crimson[i+2]=Math.round(30+175*light);
  crimson[i+3]=255;
}
fs.writeFileSync(path.join(OUT,CRIMSON.replace(/\\/g,'/')),Buffer.concat([h,crimson]));
build(false);build(true);
