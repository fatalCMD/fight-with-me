// Generated seal artwork and original feathered geometry. FWM painted textures plus stock Skyrim glints.
const path=require('path');
const {piece,vertex,band,sprite,meshWriter,FX}=require('../fx/meshkit');
const m=meshWriter(),children=[];
function part(i,p,texture,scale=1){
 const shape=m.shape('Arcane'+i,p,{texture,scale,soft:i===2||i===4?5:0,hard:i!==2&&i!==4});
 const content=i>=6?m.billboard('SurgeFacing'+i,[shape]):shape;
 children.push(m.addBlock({type:'NiNode',name:m.str('Surge'+String(i).padStart(2,'0')),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children:[content],effects:[]}));
}
function decal(size){const p=piece();sprite(p,size);return p;}
part(0,decal(142),'textures\\FightWithMe\\ArcaneSurgeSeal.dds');
part(1,decal(1),FX.glowSpot); // Hidden compatibility slot.
function curtain(r,height,seed){
 const p=piece(),segments=256,rows=16;
 for(let s=0;s<=segments;s++)for(let j=0;j<=rows;j++){
  const a=s/segments*Math.PI*2,f=j/rows,h=height*(.88+.08*Math.sin(a*5+seed)+.04*Math.sin(a*9));
  const rr=r+1.5*Math.sin(a*7+f*3)*f;
  const alpha=Math.pow(1-f,2.4)*Math.min(1,f*16)*(.7+.3*Math.sin(a*13+seed)**2);
  p.verts.push(vertex([Math.cos(a)*rr,Math.sin(a)*rr,f*h],[Math.cos(a),Math.sin(a),0],alpha,1,[s/segments*4,1-f]));
 }
 for(let s=0;s<segments;s++)for(let j=0;j<rows;j++){const a=s*(rows+1)+j,b=a+rows+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
 return p;
}
part(2,curtain(65,46,0),'textures\\FightWithMe\\HealingPulse.dds',1.1);
part(3,decal(190),'textures\\FightWithMe\\ArcaneSurgeGrandSeal.dds');
part(4,curtain(85,57,2),'textures\\FightWithMe\\HealingPulse.dds',1.1);
const glow=piece();band(glow,39,94,0,[0,.1,.45,.8,.3,0],256);part(5,glow,FX.glowSoft,.8);
for(let i=6;i<30;i++){const p=piece();sprite(p,3.4);part(i,p,FX.glowSpot,1);}
const root=m.node('BSFadeNode','ArcaneSurge',children);
for(let i=0;i<4;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/ArcaneSurge'+(i?i+1:'')+'.nif'),root);
