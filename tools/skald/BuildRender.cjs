// Export the actual shipped NIF geometry and source descriptions for the visual review.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),nif=require('../wolfhead/nifio.js');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'tools/wolfhead/preview/out');
const names=['Fervor','Echoes','Hollow','Glory'];
const exportMesh=name=>{
 const data=nif.read(fs.readFileSync(path.join(root,'assets/meshes/FightWithMe',name+'.nif')));
 const shapes=data.blocks.filter(b=>b.type==='BSTriShape').map(b=>{
  const shader=data.blocks[b.shader];
  const painted=shader.source.includes('\\Skald\\');
  const texture=painted?'painted/'+path.win32.basename(shader.source,'.dds')+'.png':'textures/'+crypto.createHash('sha1').update(shader.source.toLowerCase()).digest('hex').slice(0,12)+'.png';
  if(!fs.existsSync(path.join(out,texture)))throw Error('Missing converted texture '+shader.source);
  return {name:data.strings[b.name],positions:b.verts.flatMap(v=>v.pos),uvs:b.verts.flatMap(v=>v.uv.map(nif.half)),
   colors:b.verts.flatMap(v=>v.c.map(c=>c/255)),indices:b.tris.flat(),texture,
   shader:{scale:shader.colorScale,uvScale:shader.uvScale,uvOffset:shader.uvOffset,source:shader.source,painted},
   transform:{translate:b.translate,rotate:b.rotate,scale:b.scale}};
 });
 return {name,shapes};
};
const meshes=names.map(n=>exportMesh('Skald'+n)),lute=exportMesh('SkaldLute0'),hits=names.map((_,i)=>exportMesh('SkaldHit'+i));
const source=fs.readFileSync(path.join(root,'src/Paths.cpp'),'utf8');
const descriptions=Object.fromEntries([...source.matchAll(/case Perk::(k\w+): return Trf\("((?:[^"\\]|\\.)*)",Value\(a_perk,std::max\(1,a_rank\)\)\);/g)].map(m=>[m[1],JSON.parse('"'+m[2]+'"')]));
const values=Object.fromEntries([...source.matchAll(/case Perk::(k\w+): return \{([\d.f,]+)\};/g)].map(m=>[m[1],m[2].split(',').map(n=>Number(n.replace('f','')))]));
const policy=fs.readFileSync(path.join(root,'include/FWM/SkaldPolicy.h'),'utf8');
const number=name=>{const match=policy.match(new RegExp(name+' = ([\\d.]+)f?;'));if(!match)throw Error('Missing wave constant '+name);return Number(match[1]);};
const data={names,meshes,lute,hits,particleTypes:require('./ParticleTypes.cjs').families,tracks:JSON.parse(fs.readFileSync(path.join(__dirname,'beat-map.json'),'utf8')),
 wave:{reach:number('kReach'),speed:number('kWaveSpeed'),meshRadius:number('kWaveMeshRadius'),contact:number('kWaveContactRadius'),slots:number('kWaveSlots'),hitSeconds:number('kHitSeconds'),hitRadius:number('kHitRadius')},
 particles:{bard:number('kBardMotes'),hit:number('kHitMotes'),wave:number('kWaveMotes'),bursts:number('kHitBursts'),hitSeconds:number('kHitParticleSeconds')},
 colors:[[1,.48,.12],[.36,.95,1],[.57,.18,1],[1,.14,.21]],descriptions,values};
fs.writeFileSync(path.join(out,'skald.json'),JSON.stringify(data));
fs.copyFileSync(path.join(__dirname,'native-ljodhs.json'),path.join(out,'native-ljodhs.json'));
const coords=lute.shapes.flatMap(s=>s.positions);
console.log({meshes:meshes.length+5,descriptions:Object.keys(descriptions).length,luteBounds:[0,1,2].map(axis=>{const c=coords.filter((_,i)=>i%3===axis);return [Math.min(...c),Math.max(...c)];})});
