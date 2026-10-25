// Stock Skyrim Wispmother, posed with weighted skinning into a joined-hands prayer.
// No actor is spawned, and no third-party model or texture is required.
const path=require('path');
const {open}=require('../wolfhead/bsa');
const nif=require('../wolfhead/nifio');
const {piece,vertex,ball,band,sprite,meshWriter,FX,add,sub,mul,norm,dot,cross}=require('../fx/meshkit');
const archive=open('X:/!--- Main/Documents/Nolvus/STOCK GAME/Data/Skyrim - Meshes0.bsa');
function read(file){const entry=archive.files.find(f=>f.path.toLowerCase()===file.toLowerCase());if(!entry)throw Error(file);return nif.read(archive.read(entry));}
const source=read('meshes\\actors\\wisp\\character assets\\wisp.nif');
const m=meshWriter(),children=[],I=[1,0,0,0,1,0,0,0,1];
const apply=(r,v)=>[0,1,2].map(k=>r[k*3]*v[0]+r[k*3+1]*v[1]+r[k*3+2]*v[2]);
const name=b=>source.strings[b.name];
const bone=n=>source.blocks.find(b=>b.type==='NiNode'&&name(b)===n).translate;
function hidden(label,kids){return m.addBlock({type:'NiNode',name:m.str(label),extra:[],controller:-1,flags:0x8000e,translate:[0,0,0],rotate:I,scale:.0001,collision:-1,children:kids,effects:[]});}
// Minimal rotation taking one bone axis onto another. Applies to points and normals.
function rotation(a,b){a=norm(a);b=norm(b);const v=cross(a,b),c=dot(a,b);return p=>add(add(p,cross(v,p)),mul(cross(v,cross(v,p)),1/(1+c)));}
const poses={};
for(const [letter,sign]of[['L',-1],['R',1]]){
 const S=bone(`Wisp ${letter} Upperarm`),E=bone(`Wisp ${letter} Forearm`),W=bone(`Wisp ${letter} Hand`),target=[sign*1.5,16,108];
 const L1=Math.hypot(...sub(E,S)),L2=Math.hypot(...sub(W,E)),d=Math.hypot(...sub(target,S)),axis=norm(sub(target,S));
 const a=(L1*L1-L2*L2+d*d)/(2*d),h=Math.sqrt(Math.max(0,L1*L1-a*a));
 const outward=[sign,0,-1],perp=norm(sub(outward,mul(axis,dot(axis,outward))));
 const elbow=add(add(S,mul(axis,a)),mul(perp,h));
 const upper=rotation(sub(E,S),sub(elbow,S)),fore=rotation(sub(W,E),sub(target,elbow));
 const long=norm(sub(bone(`Wisp ${letter} Finger2 1`),W));
 const across0=sub(bone(`Wisp ${letter} Finger1 1`),bone(`Wisp ${letter} Finger2 1`));
 const across=norm(sub(across0,mul(long,dot(across0,long)))),normal=cross(long,across);
 const newLong=norm([sign*.1,0,1]),newNormal=[-sign,0,sign*sign*.1],newAcross=norm(cross(newNormal,newLong));
 const newNorm=cross(newLong,newAcross);
 const hand=p=>add(add(mul(newLong,dot(p,long)),mul(newAcross,dot(p,across))),mul(newNorm,dot(p,normal)));
 poses[letter]={S,E,W,target,elbow,upper,fore,hand};
}
function pose(boneName,p,n){
 const match=boneName.match(/^Wisp ([LR]) (.+)$/);if(!match)return [p,n];
 const q=poses[match[1]],part=match[2];
 if(/Upperarm/i.test(part))return [add(q.S,q.upper(sub(p,q.S))),q.upper(n)];
 if(/Forearm/i.test(part))return [add(q.elbow,q.fore(sub(p,q.E))),q.fore(n)];
 if(/Hand|Finger|Thumb/i.test(part))return [add(q.target,q.hand(sub(p,q.W))),q.hand(n)];
 return [p,n];
}
const body=[];let minZ=Infinity,maxZ=-Infinity;
for(const s of source.blocks.filter(b=>b.type==='BSTriShape'&&b.skin>=0&&!/Magic|^ArmFabric$|^SkirtFabric/i.test(name(b)))){
 const skin=new nif.Reader(source.blocks[s.skin].raw),dataIndex=skin.i32(),partition=skin.i32();skin.i32();const bones=skin.refs();
 const data=new nif.Reader(source.blocks[dataIndex].raw);
 const transform=()=>({r:Array.from({length:9},()=>data.f32()),t:Array.from({length:3},()=>data.f32()),s:data.f32()});
 transform();const count=data.u32(),weighted=data.u8(),bind=[];
 for(let i=0;i<count;i++){bind.push(transform());data.p+=16;const nv=data.u16();if(weighted)data.p+=nv*6;}
 if(data.p!==data.buf.length)throw Error('skin layout');
 const sp=nif.readSkinPartition(source.blocks[partition].raw),p=piece();
 for(const v of sp.verts){let pos=[0,0,0],normal=[0,0,0],total=0;
  for(let k=0;k<4;k++){const w=nif.half(v.w[k]);if(w<=0)continue;const node=source.blocks[bones[v.bi[k]]],b=bind[v.bi[k]];
   const local=add(mul(apply(b.r,v.pos),b.s),b.t),world=add(mul(apply(node.rotate,local),node.scale),node.translate);
   const direction=apply(node.rotate,apply(b.r,v.n.map(x=>x/255*2-1)));
   const [posed,n]=pose(name(node),world,direction);pos=add(pos,mul(posed,w));normal=add(normal,mul(n,w));total+=w;
  }
  pos=mul(pos,1/(total||1));minZ=Math.min(minZ,pos[2]);maxZ=Math.max(maxZ,pos[2]);
  const shade=.35+.65*Math.max(0,dot(norm(normal),norm([-.35,.65,.8])));
  p.verts.push(vertex(pos,normal,1,shade,v.uv.map(nif.half)));
 }
 for(const part of sp.parts)p.tris.push(...part.trisCopy);
 const shader=source.blocks[s.shader],texture=shader.textureSet>=0?source.blocks[shader.textureSet].textures[0]:shader.source;
 body.push(m.shape('Prayer_'+name(s),p,{texture,scale:1.3,rim:[1,0,.4,1],soft:8}));
}
children.push(hidden('Mother',body));
const moon=piece();ball(moon,29,5,{repeat:1});
children.push(hidden('Moon',[m.shape('LunarSphere',moon,{texture:FX.cloud,scale:1.4,rim:[1,0,.4,1],soft:2})]));
const star=read('meshes\\clutter\\daedric\\azurastar.nif'),starShapes=[];
for(const s of star.blocks.filter(b=>b.type==='BSTriShape'&&b.verts?.length)){
 const p=piece(),lo=[0,1,2].map(k=>Math.min(...s.verts.map(v=>v.pos[k]))),hi=[0,1,2].map(k=>Math.max(...s.verts.map(v=>v.pos[k])));
 const centre=lo.map((v,k)=>(v+hi[k])/2),size=Math.max(...hi.map((v,k)=>v-lo[k]));
 console.log('Star bounds',lo,hi);
 // The stock artifact lies in YZ. Turn its face towards +Y.
 for(const v of s.verts){const q=mul(sub(v.pos,centre),83/size),n=v.n.map(x=>x/255*2-1);
  p.verts.push(vertex([q[1],q[0],q[2]],[n[1],n[0],n[2]],1,.65+.35*Math.abs(n[0]),v.uv.map(nif.half)));
 }
 p.tris=s.tris;const shader=star.blocks[s.shader],texture=star.blocks[shader.textureSet].textures[0];
 starShapes.push(m.shape('AzurasStar',p,{texture,scale:2.3,rim:[1,0,.6,1],soft:2}));
}
children.push(hidden('Star',starShapes));
function glow(label,size){const p=piece();sprite(p,size);return m.billboard(label+'Facing',[m.shape(label+'Light',p,{texture:FX.glowSpot,scale:1.5,soft:6})]);}
children.push(hidden('Halo',[glow('CelestialHalo',170)]));
for(const [label,radius]of[['Wave',50],['Mist',90]]){const p=piece();band(p,radius*.7,radius,0,[0,.12,.8,.25,0],192);children.push(hidden(label,[m.shape(label+'Texture',p,{texture:FX.cloud,scale:2.6,soft:8})]));}
// Six suspended crescents, six soft scalloped veils, and quiet four-point starlight.
for(let k=0;k<12;k++){
 const p=piece(),rows=72,cols=8;
 for(let j=0;j<=rows;j++)for(let i=0;i<=cols;i++){
  const f=j/rows,u=i/cols;
  if(k<6){
   const a=-Math.PI*.81+f*Math.PI*1.62,outer=10,inner=outer-(3.5*Math.sin(f*Math.PI)),r=inner+(outer-inner)*u;
   p.verts.push(vertex([Math.cos(a)*r,0,Math.sin(a)*r],[0,1,0],Math.sin(f*Math.PI)**.4*Math.sin(u*Math.PI)**.5,1,[.5,.5]));
  }else{
   const a=(u-.5)*1.15,r=34+4*Math.sin(f*Math.PI);
   p.verts.push(vertex([Math.sin(a)*r,Math.cos(a)*r,f*133],[Math.sin(a),Math.cos(a),0],Math.sin(f*Math.PI)**.8*Math.sin(u*Math.PI)**1.5,.8,[u,f]));
  }
 }
 for(let j=0;j<rows;j++)for(let i=0;i<cols;i++){const a=j*(cols+1)+i,b=a+cols+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
 const label='Lunar'+String(k).padStart(2,'0');children.push(hidden(label,[m.shape(label+'Blessing',p,{texture:k<6?FX.glowSpot:FX.cloud,scale:k<6?2.5:1.8,soft:8})]));
}
for(let i=0;i<48;i++){
 const label='Mote'+String(i).padStart(2,'0'),p=piece();
 // Geometric stars keep their points without a rectangular billboard texture.
 p.verts.push(vertex([0,0,0],[0,0,1],1,1));
 for(let j=0;j<8;j++){const a=j*Math.PI/4,r=j%2?.55:3.8;p.verts.push(vertex([Math.cos(a)*r,Math.sin(a)*r,0],[0,0,1],j%2?.7:0,1));}
 for(let j=0;j<8;j++)p.tris.push([0,1+j,1+(j+1)%8]);
 children.push(hidden(label,[m.billboard(label+'Facing',[m.shape(label+'Star',p,{texture:FX.glowSpot,scale:3,soft:4})])]));
}
for(let i=0;i<48;i++){
 const label='Wrap'+String(i).padStart(2,'0'),segment=i%8,p=piece(),rows=5,cols=8;
 for(let j=0;j<=rows;j++)for(let k=0;k<=cols;k++){
  const t=j/rows,u=k/cols,f=(segment+t)/8,width=.95*(1-.7*f);
  p.verts.push(vertex([(u-.5)*width,.025*Math.sin(u*Math.PI*3),t],[0,1,0],Math.sin(u*Math.PI)**.7*(1-f**6),.8,[u,f*3]));
 }
 for(let j=0;j<rows;j++)for(let k=0;k<cols;k++){const a=j*(cols+1)+k,b=a+cols+1;p.tris.push([a,b,a+1],[a+1,b,b+1]);}
 children.push(hidden(label,[m.shape(label+'Fabric',p,{texture:'textures\\actors\\wisp\\WispClothTile.dds',scale:1.5,soft:5})]));
}
const bodyAura=piece();sprite(bodyAura,240,{alpha:.48});
for(const v of bodyAura.verts)v.pos[0]*=.6;
const glowNode=m.billboard('MotherGlowFacing',[m.shape('ApparitionGlow',bodyAura,{texture:FX.glowSpot,scale:1.4,soft:18})]);
const glowCentre=m.addBlock({type:'NiNode',name:m.str('MotherGlowCentre'),extra:[],controller:-1,flags:0x8000e,translate:[0,0,80],rotate:I,scale:1,collision:-1,children:[glowNode],effects:[]});
children.push(hidden('MotherAura',[glowCentre]));
const root=m.node('BSFadeNode','AzurasMoon',children);
for(let i=0;i<24;i++)m.write(path.resolve(__dirname,'../../assets/meshes/FightWithMe/AzurasMoon'+(i?i+1:'')+'.nif'),root);
console.log('Prayer body height',minZ,maxZ);
