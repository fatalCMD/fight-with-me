// Original rage mantle geometry. Only vanilla Skyrim fire and ember textures are referenced.
const path=require('path');
const {piece,vertex,band,sprite,meshWriter,FX}=require('../fx/meshkit');
for(let copy=0;copy<4;copy++){
 const m=meshWriter(),children=[];
 for(const name of ['Shockwave','Aftershock']){
  const p=piece();band(p,35,60,0,[0,.25,.65,.25,0],128);
  children.push(m.node('NiNode',name,[m.shape(name+'Flame',p,{texture:FX.fireTile,scale:1.4,soft:12})]));
 }
 for(let k=0;k<12;k++){
  const p=piece();
  // Two curved, feathered flame surfaces per plume. Alpha reaches zero along every edge.
  for(let side=0;side<2;side++){
   const base=p.verts.length,rows=24,cols=12,angle=side*Math.PI/2;
   for(let j=0;j<=rows;j++)for(let i=0;i<=cols;i++){
    const f=j/rows,u=i/cols,wide=42*(1-.75*f),bend=17*Math.sin(f*3.8+k*.65)*f;
    const x=(u-.5)*wide,b=6*Math.sin(f*5+k)*f;
    const v=vertex([Math.cos(angle)*x+bend,Math.sin(angle)*x+b,f*138],
      [Math.sin(angle),-Math.cos(angle),0],Math.pow(Math.sin(Math.PI*u),1.4)*Math.pow(Math.sin(Math.PI*f),.8)*.7,1,[u,1-f*1.8]);
    p.verts.push(v);
   }
   for(let j=0;j<rows;j++)for(let i=0;i<cols;i++){const a=base+j*(cols+1)+i,b=a+cols+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
  }
  const name='Plume'+String(k).padStart(2,'0');
  children.push(m.node('NiNode',name,[m.shape(name+'Fire',p,{texture:FX.fireTile,scale:1.9,soft:14})]));
 }
 for(let k=0;k<48;k++){
  const p=piece();sprite(p,3.6,{alpha:.8});const name='Ember'+String(k).padStart(2,'0');
  children.push(m.node('NiNode',name,[m.billboard(name+'Facing',[m.shape(name+'Glow',p,{texture:FX.glowSpot,scale:2,soft:4})])]));
 }
 m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/BerserkMantle'+(copy?copy+1:'')+'.nif'),m.node('BSFadeNode','Berserk',children));
}
