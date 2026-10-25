// Generated rune artwork with original air ribbons and Skyrim soft glints.
const path=require('path');
const {piece,vertex,sprite,meshWriter,FX}=require('../fx/meshkit');
const m=meshWriter(),children=[];
function hidden(i,children){return m.addBlock({type:'NiNode',name:m.str('Part'+String(i).padStart(2,'0')),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:[1,0,0,0,1,0,0,0,1],scale:.0001,collision:-1,children,effects:[]});}
function strip(out,points,width,weight=()=>1){
 const base=out.verts.length;
 for(let i=0;i<points.length;i++){
  const [x,z]=points[i],before=points[Math.max(0,i-1)],after=points[Math.min(points.length-1,i+1)],dx=after[0]-before[0],dz=after[1]-before[1],len=Math.hypot(dx,dz)||1;
  for(let j=0;j<3;j++){const xx=x-dz/len*(j-1)*width,zz=z+dx/len*(j-1)*width;out.verts.push(vertex([xx,0,zz],[0,-1,0],j===1?weight(Math.hypot(xx,zz),i/(points.length-1)):0));}
 }
 for(let i=0;i<points.length-1;i++)for(let j=0;j<2;j++){const a=base+i*3+j,b=a+3;out.tris.push([a,b,a+1],[a+1,b,b+1]);}
}
const polar=(r,t)=>[r*Math.cos(t),r*Math.sin(t)];
// Two generated, transparent spell wheels; never tint over the rank III colour gradient.
for(let layer=0;layer<4;layer++){
 const p=piece();sprite(p,210);
 for(const v of p.verts){v.pos=[v.pos[0],0,v.pos[1]];v.n=[128,0,128];}
 const texture='textures\\FightWithMe\\'+(layer===1?'ArrowRuneGrand':'ArrowRune')+'.dds';
 children.push(hidden(layer,[m.shape('GeneratedRune'+layer,p,{texture,scale:1,hard:true})]));
}
for(let i=4;i<7;i++){
 const p=piece();strip(p,Array.from({length:113},(_,j)=>{const t=j/112;return polar(17+6*t,t*Math.PI*1.75+(i-4)*2.094);}),3.5,(_,t)=>Math.pow(Math.sin(t*Math.PI),.65));
 children.push(hidden(i,[m.shape('AirRibbon'+i,p,{scale:.8,hard:true})]));
}
for(let i=7;i<40;i++){
 const p=piece();sprite(p,i===7?100:i<16?9:4);if(i>=16)for(const v of p.verts){v.pos[0]*=2;v.pos[1]*=.5;}
 children.push(hidden(i,[m.billboard('GlintFacing'+i,[m.shape('Glow'+i,p,{texture:FX.glowSpot,scale:i===7?1.1:.85})])]));
}
const root=m.node('BSFadeNode','ArrowArts',children);
for(let i=0;i<24;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/ArrowArts'+(i?i+1:'')+'.nif'),root);
