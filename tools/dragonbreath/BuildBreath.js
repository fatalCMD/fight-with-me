// Broad, level elemental beams. Only Skyrim textures; the geometry and motion are original.
const path=require('path'),{piece,vertex,ribbon,meshWriter,FX,random,flake,ball,norm,cross,sub}=require('../fx/meshkit');
const ease=t=>{t=Math.max(0,Math.min(1,t));return t*t*(3-2*t);};
const profile=f=>.16+.84*ease(f/.12),tail=f=>ease(f/.006)*(1-ease((f-.86)/.14));
// Closed round cross-sections, never crossed rectangular cards. Every end tapers to zero opacity.
function tube(out,points,radius,alpha,sides=16,tile=3) {
 const base=out.verts.length;
 for(let j=0;j<points.length;j++) {
  const f=j/(points.length-1),p=points[j],d=norm(sub(points[Math.min(j+1,points.length-1)],points[Math.max(0,j-1)]));
  const u=norm(cross(d,Math.abs(d[2])>.9?[1,0,0]:[0,0,1])),v=norm(cross(d,u));
  const r=typeof radius==='function'?radius(f):radius,a=typeof alpha==='function'?alpha(f):alpha;
  for(let k=0;k<=sides;k++) {const t=k/sides*Math.PI*2,n=u.map((x,i)=>x*Math.cos(t)+v[i]*Math.sin(t));out.verts.push(vertex(p.map((x,i)=>x+n[i]*r),n,a,1,[tile?k/sides*tile:.5,tile?f*3:.5]));}
 }
 for(let j=0;j<points.length-1;j++)for(let k=0;k<sides;k++){const a=base+j*(sides+1)+k,b=a+sides+1;out.tris.push([a,b,a+1],[a+1,b,b+1]);}
}
function softPuff(out,radius,cols=1,rows=1,cell=0){
 const segments=24,rings=6,col=cell%cols,row=Math.floor(cell/cols)%rows;
 const put=(x,y,a)=>out.verts.push(vertex([x*radius,y*radius,0],[0,0,1],a,1,[(col+.5+x*.5)/cols,(row+.5-y*.5)/rows]));
 put(0,0,.7);
 for(let ring=1;ring<=rings;ring++)for(let k=0;k<=segments;k++) {const f=ring/rings,a=k/segments*Math.PI*2;put(Math.cos(a)*f,Math.sin(a)*f,.7*(1-ease(f))**1.5);}
 const at=(ring,k)=>1+(ring-1)*(segments+1)+k;
 for(let k=0;k<segments;k++)out.tris.push([0,at(1,k),at(1,k+1)]);
 for(let ring=1;ring<rings;ring++)for(let k=0;k<segments;k++) {const a=at(ring,k),b=at(ring+1,k);out.tris.push([a,b,a+1],[a+1,b,b+1]);}
}
for(let rank=1;rank<=3;rank++)for(const [element,name] of ['Fire','Frost','Shock'].entries()) {
 const m=meshWriter(),rand=random(530+element),parts=[],radius=[90,125,180][rank-1]*1000/[600,850,1100][rank-1];
 for(let k=0;k<8;k++) {
  const p=piece(),points=[];
  for(let j=0;j<=96;j++){const f=j/96,a=k*2.399+f*7,r=radius*profile(f)*ease(f/.04)*(k<3?.06:rank===3&&element===0?.68:.45);points.push([Math.cos(a)*r,f*1000,Math.sin(a)*r]);}
  tube(p,points,f=>profile(f)*radius*(k<3?.38:rank===3&&element===0?.25:.46)*(1+.11*Math.sin(f*43+k)),f=>tail(f)*(element===2?.018:element===1?(rank===3?.22:.14):.34),16);
  parts.push(m.node('NiNode','Jet'+k,[m.shape('Flow'+k,p,{scale:element===2?1.8:element===1?2:rank===3&&k<3?2.5:1.4,texture:element===0?FX.fireTile:element===1?FX.frostCloud:FX.glowSoft,soft:40})]));
 }
 if(element===2)for(let index=0;index<64;index++){
  const main=index<8,p=piece(),points=[],start=main?0:60+(index-8)/56*840,length=main?1000:80+rand()*110;
  const steps=main?64:12,angle=index*2.399,spread=radius*(main?.12:.14);
  for(let j=0;j<=steps;j++){
   const t=j/steps,f=(start+t*length)/1000,a=angle+f*5,r=radius*profile(f)*ease(f/.07)*(main?.18+.45*(index%4)/3:.45);
   points.push([Math.cos(a)*r+(rand()-.5)*spread*Math.sin(t*Math.PI),t*length,Math.sin(a)*r+(rand()-.5)*spread*Math.sin(t*Math.PI)]);
  }
  const opacity=t=>tail((start+t*length)/1000)*(main?1:Math.sin(t*Math.PI)*.65);
  // One connected bolt from throat to tip, with an actual thin core and soft corona.
  // No tiled lightning atlas: it made repeated little zigzag rectangles.
  tube(p,points,main?radius*.035:radius*.020,t=>opacity(t)*.055,8,0);
  tube(p,points,main?radius*.007:radius*.005,t=>opacity(t)*.5,8,0);
  const shape=m.shape('Lightning'+index,p,{scale:10,texture:FX.glowSoft,soft:12});
  parts.push(m.addBlock({type:'NiNode',name:m.str('Arc'+index),extra:[],controller:-1,flags:0x8000e,translate:[0,start,0],rotate:[1,0,0,0,1,0,0,0,1],scale:1,collision:-1,children:[shape],effects:[]}));
 }
 for(let i=0;i<32;i++) {
  const p=piece();softPuff(p,radius*(element===2?.055:rank===3&&element===0?.24:.42));
  parts.push(m.node('NiNode','Puff'+i,[m.billboard('Facing'+i,[m.shape('Mist'+i,p,{scale:element===2?5:1.8,texture:element===0?FX.fireTile:element===1?FX.frostCloud:FX.spark,soft:35})])]));
 }
 if(rank===3&&element===1)for(let i=0;i<16;i++){
  const p=piece();flake(p,2+rand()*2,8+rand()*12);
  for(const v of p.verts){v.pos=[v.pos[0],v.pos[2],v.pos[1]];v.uv=[0x3800,0x3800];}
  parts.push(m.node('NiNode','Ice'+i,[m.shape('FrigidShard'+i,p,{texture:'textures\\FightWithMe\\ElementalIce.dds',scale:2.5,soft:10})]));
 }
 const extra=[];
 if(rank===3&&element===2)for(let i=0;i<4;i++) {
  const p=piece(),points=Array.from({length:21},(_,j)=>{const t=j/20;return[(rand()-.5)*14*Math.sin(t*Math.PI),t*100,(rand()-.5)*14*Math.sin(t*Math.PI)];});
  tube(p,points,.9,t=>Math.sin(t*Math.PI)*.8,8,0);tube(p,points,3,t=>Math.sin(t*Math.PI)*.12,8,0);
  const flash=piece();ball(flash,4,2,{centre:[0,100,0],alpha:()=>.35});
  extra.push(m.node('NiNode','Ground'+i,[m.shape('GroundBolt'+i,p,{texture:FX.glowSoft,scale:18,soft:8}),m.shape('GroundFlash'+i,flash,{texture:FX.glowSoft,scale:8,soft:12})]));
 }
 const root=m.node('BSFadeNode','Breath'+name,[m.node('NiNode','Stream',parts),...extra]);
 for(let copy=0;copy<4;copy++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/Breath'+name+(['','II','III'][rank-1])+(copy?copy+1:'')+'.nif'),root);
}
