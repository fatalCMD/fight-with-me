// Authored impact ribbons and metal glints; only Skyrim's glow texture is referenced.
const path=require('path');
const {piece,vertex,sprite,meshWriter,FX}=require('../fx/meshkit');
const m=meshWriter(),children=[];
function hidden(name,children){return m.addBlock({type:'NiNode',name:m.str(name),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children,effects:[]});}
function glow(name,size,stretch=1){const p=piece();sprite(p,size);for(const v of p.verts){v.pos[0]*=stretch;v.pos[1]/=stretch;}return m.billboard(name+'Facing',[m.shape(name+'Glow',p,{texture:FX.glowSpot,scale:.85})]);}
children.push(hidden('Flash',[glow('Contact',64)]));
for(let ring=0;ring<2;ring++){
 const p=piece(),steps=160;
 for(let i=0;i<=steps;i++){
  const t=i/steps*Math.PI*2,variation=1+.05*Math.sin(t*7)+.025*Math.sin(t*13);
  for(let j=0;j<3;j++){const radius=(36+(j-1)*5)*variation;
   const a=j===1?(.65+.35*Math.pow(Math.sin(t*3+ring),2)):0;
   p.verts.push(vertex([Math.cos(t)*radius,Math.sin(t)*radius,0],[0,0,1],a));
  }
 }
 for(let i=0;i<steps;i++)for(let j=0;j<2;j++){const a=i*3+j,b=a+3;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
 children.push(hidden('Ring'+ring,[m.shape('ImpactRibbon'+ring,p,{scale:.75,hard:true})]));
}
for(let i=0;i<40;i++)children.push(hidden('Spark'+String(i).padStart(2,'0'),[glow('Metal'+i,4,2.1)]));
const root=m.node('BSFadeNode','Riposte',children);
for(let i=0;i<16;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/RiposteStrike'+(i?i+1:'')+'.nif'),root);
