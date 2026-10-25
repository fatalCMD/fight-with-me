// Skyrim's upper-body bones, separated at the shoulders, elbows and wrists for a spectral gesture.
// Only the player's vanilla archives are read. Textures remain references to their installed game.
const { open } = require('../wolfhead/bsa.js');
const nifio = require('../wolfhead/nifio.js');
const { piece, vertex } = require('../fx/meshkit.js');
const archive = open('X:/!--- Main/Documents/Nolvus/STOCK GAME/Data/Skyrim - Meshes0.bsa');
const I = [1,0,0,0,1,0,0,0,1];
const sub = (a,b) => a.map((v,k)=>v-b[k]);
const apply = (m,v) => [0,1,2].map(k=>m[k*3]*v[0]+m[k*3+1]*v[1]+m[k*3+2]*v[2]);
const smooth = (a,b,x) => { const t=Math.max(0,Math.min(1,(x-a)/(b-a))); return t*t*(3-2*t); };
function read(file) {
  const full=file.startsWith('meshes\\')?file:`meshes\\actors\\draugr\\character assets\\${file}`;
  const entry=archive.files.find(f=>f.path.toLowerCase()===full);
  if (!entry) throw new Error(`Missing vanilla specter source: ${file}`);
  return nifio.read(archive.read(entry));
}

module.exports = function specter(m, grand) {
  const source=read(grand?'draugrmale.nif':'skeletonnecroskin.nif');
  const name=(n,i)=>i>=0?n.strings[i]:'';
  const bone=n=>{ const b=source.blocks.find(b=>b.type==='NiNode'&&name(source,b.name)===n); if(!b)throw new Error(`No ${n}`); return b.translate; };
  const pivots={Spirit:[0,0,82], Skull:bone('NPC Head [Head]')};
  for (const [side,letter] of [['Left','L'],['Right','R']]) {
    pivots[side+'Arm']=bone(`NPC ${letter} UpperArm [${letter}Uar]`);
    pivots[side+'Forearm']=bone(`NPC ${letter} Forearm [${letter}Lar]`);
    pivots[side+'Hand']=bone(`NPC ${letter} Hand [${letter}Hnd]`);
    for(let finger=0;finger<5;++finger)for(let joint=0;joint<3;++joint)
      pivots[`${side}Finger${finger}${joint}`]=bone(`NPC ${letter} Finger${finger}${joint} [${letter}F${finger}${joint}]`);
  }
  const children=Object.fromEntries(Object.keys(pivots).map(k=>[k,[]]));
  const classify=b=>{
    if (/Head/.test(b)) return 'Skull';
    for(const [side,letter]of[['Left','L'],['Right','R']]) {
      if(b.startsWith(`NPC ${letter} `)) {
        const finger=b.match(/Finger(\d\d)/);
        if(finger)return side+'Finger'+finger[1];
        if(/Hand/.test(b))return side+'Hand';
        if(/Forearm/i.test(b))return side+'Forearm';
        if(/Upperarm/i.test(b))return side+'Arm';
      }
    }
    if(/Thigh|Calf|Foot|Toe|Tail/.test(b))return null;
    return 'Spirit';
  };
  function gather(n, accept) {
    for (const s of n.blocks.filter(b=>b.type==='BSTriShape'&&b.skin>=0&&accept(name(n,b.name)))) {
      const skin=new nifio.Reader(n.blocks[s.skin].raw);const skinData=skin.i32();const partition=skin.i32();skin.i32();const bones=skin.refs();
      const data=new nifio.Reader(n.blocks[skinData].raw);
      const transform=()=>({r:Array.from({length:9},()=>data.f32()),t:Array.from({length:3},()=>data.f32()),s:data.f32()});
      transform();const count=data.u32(),weighted=data.u8(),bind=[];
      for(let i=0;i<count;++i){bind.push(transform());data.p+=16;const nv=data.u16();if(weighted)data.p+=nv*6;}
      if(data.p!==data.buf.length||count!==bones.length)throw new Error('Unexpected skin binding data');
      const sp=nifio.readSkinPartition(n.blocks[partition].raw);
      const shader=n.blocks[s.shader];
      const texture=shader.textureSet>=0?n.blocks[shader.textureSet].textures[0]:shader.source;
      const verts=sp.verts.map(v=>{
        const weights={},pos=[0,0,0],normal=[0,0,0];let total=0;
        for(let k=0;k<4;++k){
          const w=nifio.half(v.w[k]);if(w<=0)continue;
          const original=n.blocks[bones[v.bi[k]]];if(!original)throw new Error('Invalid global bone index');
          const boneName=name(n,original.name),node=source.blocks.find(b=>b.type==='NiNode'&&name(source,b.name)===boneName)||original;
          const part=classify(boneName);weights[part]=(weights[part]||0)+w;
          // Skin-to-bone then the canonical resting bone: exports store different bind poses.
          const b=bind[v.bi[k]];
          const local=apply(b.r,v.pos).map((x,j)=>x*b.s+b.t[j]);
          const at=apply(node.rotate,local).map((x,j)=>x*node.scale+node.translate[j]);
          const direction=apply(node.rotate,apply(b.r,v.n.map(x=>x/255*2-1)));
          for(let j=0;j<3;++j){pos[j]+=at[j]*w;normal[j]+=direction[j]*w;}total+=w;
        }
        for(let j=0;j<3;++j)pos[j]/=total||1;
        const group=Object.entries(weights).sort((a,b)=>b[1]-a[1])[0]?.[0];
        return {...v,pos,normal,group};
      });
      for(const [group,pivot] of Object.entries(pivots)) {
        const p=piece(), index=new Map();
        for(const part of sp.parts)for(const tri of part.trisCopy){
          const corners=tri.map(i=>verts[i]);
          const votes={};for(const v of corners)votes[v.group]=(votes[v.group]||0)+1;
          if(Object.entries(votes).sort((a,b)=>b[1]-a[1])[0][0]!==group)continue;
          if(group==='Spirit'&&corners.some(v=>v.pos[2]<82))continue;
          p.tris.push(tri.map(i=>{
            if(!index.has(i)){
              const v=verts[i], fade=group==='Spirit'?smooth(82,103,v.pos[2]):1;
              index.set(i,p.verts.length);
              const crown=name(n,s.name)==='Bone Crown';
              const bright=crown?0.35+0.65*Math.max(0,(v.normal[1]*0.5+v.normal[2]*0.8)):1;
              p.verts.push(vertex(sub(v.pos,pivot),v.normal,fade,bright,crown?[0.5,0.5]:v.uv.map(nifio.half)));
            }
            return index.get(i);
          }));
        }
        if(p.tris.length)children[group].push(m.shape(`${group}_${name(n,s.name)}`,p,{scale:grand?0.95:1.2,texture:name(n,s.name)==='Bone Crown'?undefined:texture,rim:[1,0,0.48,1],soft:6}));
      }
    }
  }
  gather(source,s=>grand?s==='Body_Male_Naked':!s.endsWith('01'));
  if(grand) gather(read('draugrmale07.nif'),s=>['ArmorChest00','ArmorBracers00','Armor_Pauldrons00'].includes(s));

  // Skyrim's Jagged Crown, fitted through the same head bone; no added eye lights.
  if(grand) gather(read('meshes\\armor\\bonecrown\\bonecrownm.nif'),()=>true);
  function node(label,parent){
    return m.addBlock({type:'NiNode',name:m.str(label),extra:[],controller:-1,flags:0x8000e,translate:parent?sub(pivots[label],pivots[parent]):[0,0,0],rotate:I,scale:1,collision:-1,children:children[label],effects:[]});
  }
  children.Spirit.push(node('Skull','Spirit'));
  for(const side of ['Left','Right']){
    children.Spirit.push(node(side+'Hand','Spirit'));
    children.Spirit.push(node(side+'Forearm','Spirit'));
    children.Spirit.push(node(side+'Arm','Spirit'));
    for(let finger=0;finger<5;++finger)for(let joint=0;joint<3;++joint)
      children.Spirit.push(node(`${side}Finger${finger}${joint}`,'Spirit'));
  }
  return node('Spirit');
};
