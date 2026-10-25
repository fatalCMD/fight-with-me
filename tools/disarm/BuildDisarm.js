// Original tapered cuts and glints; only our Glow and vanilla Skyrim glow texture.
const path=require('path');
const {piece,vertex,sprite,meshWriter,FX}=require('../fx/meshkit');
const m=meshWriter(),children=[];
function hidden(name,children){return m.addBlock({type:'NiNode',name:m.str(name),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children,effects:[]});}
function glint(name,size){const p=piece();sprite(p,size);if(name.startsWith('Glint')){const angle=Number(name.slice(5))*2.39996323;for(const v of p.verts){const x=v.pos[0]*2.3,y=v.pos[1]*.36;v.pos=[x*Math.cos(angle)-y*Math.sin(angle),x*Math.sin(angle)+y*Math.cos(angle),0];}}return m.billboard(name+'Facing',[m.shape(name+'Glow',p,{texture:FX.glowSpot,scale:.9})]);}
children.push(hidden('Flash',[glint('Contact',52)]));
for(let side=0;side<2;side++){
 const pieces=[];
 for(const [width,strength] of [[9,.16],[3.1,.95]]){
  const p=piece(),steps=64,theta=(side?-.25:.25)*Math.PI;
  for(let i=0;i<=steps;i++){
   const f=i/steps,x=(f-.5)*100,y=Math.sin(f*Math.PI)*3,edge=Math.pow(Math.sin(f*Math.PI),.65);
   for(let j=0;j<3;j++){
    const yy=y+(j-1)*width*edge;
    p.verts.push(vertex([x*Math.cos(theta)-yy*Math.sin(theta),x*Math.sin(theta)+yy*Math.cos(theta),0],[0,0,1],j===1?edge*strength:0));
   }
  }
  for(let i=0;i<steps;i++)for(let j=0;j<2;j++){const a=i*3+j,b=a+3;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
  pieces.push(m.shape('Cut'+side+'Width'+width,p,{scale:1,hard:true}));
 }
 children.push(hidden('Slash'+side,[m.billboard('CutFacing'+side,pieces)]));
}
for(let i=0;i<32;i++)children.push(hidden('Spark'+String(i).padStart(2,'0'),[glint('Glint'+i,5)]));
const root=m.node('BSFadeNode','Disarm',children);
// Hidden authored nodes avoid a first-frame burst before runtime has sampled the animation.
// Runtime restores each part only after its first sampled pose.
for(let i=0;i<16;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/DisarmStrike'+(i?i+1:'')+'.nif'),root);
