// Original open wind ribbons, feathered energy ripples and drifting glints.
// Authored geometry with stock Skyrim CloudTile and glow textures only.
const path=require('path');
const {piece,vertex,band,ball,sprite,meshWriter,FX}=require('../fx/meshkit');
const m=meshWriter(),children=[];
function hidden(name,children){return m.addBlock({type:'NiNode',name:m.str(name),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children,effects:[]});}
for(const name of ['Pulse','Echo']){
 const p=piece();band(p,29,51,0,[0,.08,.5,.12,0],160);
 children.push(hidden(name,[m.shape(name+'Mist',p,{texture:FX.cloud,scale:2.2,soft:12})]));
}
function glow(name,size){const p=piece();sprite(p,size);return m.billboard(name+'Facing',[m.shape(name+'Glow',p,{texture:FX.glowSpot,scale:1,soft:6})]);}
const heart=piece();ball(heart,43,3,{alpha:()=>.24,repeat:2});
children.push(hidden('Heart',[m.shape('BreathShell',heart,{texture:FX.cloud,scale:1.8,soft:16})]));
for(let k=0;k<12;k++){
 const p=piece(),rows=72,cols=8;
 for(let j=0;j<=rows;j++)for(let i=0;i<=cols;i++){
  const f=j/rows,u=i/cols,angle=f*2.5+(k%2?.12:-.12)*Math.sin(f*Math.PI*2);
  const radius=27+6*Math.sin(f*3.5+k*.35),width=(u-.5)*(.23+.19*Math.sin(f*Math.PI));
  const a=angle+width,z=f*112+3*Math.sin(f*6+k);
  const alpha=Math.pow(Math.sin(Math.PI*f),.8)*Math.pow(Math.sin(Math.PI*u),1.5);
  p.verts.push(vertex([Math.cos(a)*radius,Math.sin(a)*radius,z],[Math.cos(a),Math.sin(a),0],alpha,.9,[u,f*1.7]));
 }
 for(let j=0;j<rows;j++)for(let i=0;i<cols;i++){const a=j*(cols+1)+i,b=a+cols+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
 const name='Wisp'+String(k).padStart(2,'0');
 children.push(hidden(name,[m.shape(name+'Wind',p,{texture:FX.cloud,scale:2.6,soft:10})]));
}
for(let i=0;i<56;i++){const name='Mote'+String(i).padStart(2,'0');children.push(hidden(name,[glow(name,4.5)]));}
const root=m.node('BSFadeNode','SecondWind',children);
for(let i=0;i<8;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/SecondWind'+(i?i+1:'')+'.nif'),root);
