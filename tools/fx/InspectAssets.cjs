const fs=require('fs'),path=require('path'),assert=require('assert');
const nifio=require('../wolfhead/nifio');
const root=path.resolve(process.argv[2]||path.join(__dirname,'../../assets'));
const meshes=[];
function walk(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){
 const file=path.join(dir,e.name);if(e.isDirectory())walk(file);else if(e.name.endsWith('.nif')){
  const bytes=fs.readFileSync(file),nif=nifio.read(bytes),textures=new Set();
  for(const b of nif.blocks){
   if(b.type==='BSEffectShaderProperty'){if(b.source)textures.add(b.source);if(b.greyscale)textures.add(b.greyscale);}
   if(b.type==='BSShaderTextureSet')for(const t of b.textures)if(t)textures.add(t);
   if(b.type==='BSTriShape'){
    for(const v of b.verts)assert(v.pos.every(Number.isFinite),'nonfinite vertex '+file);
    for(const tri of b.tris)assert(tri.every(i=>i>=0&&i<b.verts.length),'invalid triangle '+file);
   }
  }
  if (/^(WarCrySabre|WarCryDragon|SpectralBearHead)/.test(e.name)) {
   assert(nifio.write(nif).equals(bytes),'Head NIF read/write mismatch');
   for(const name of ['Head','Jaw','Wave0','Wave1','Wave2'])assert(nif.strings.includes(name),'missing spectral head node '+name);
   assert(nif.blocks.filter(b=>b.type==='BSTriShape').every(b=>b.skin===-1),'spectral head must be rigid, not an actor body');
  }
  if(/^Windstep(?:Female)?\d*\.nif$/.test(e.name)){
   assert(nifio.write(nif).equals(bytes),'Windstep NIF roundtrip mismatch');
   for(let i=0;i<52;i++)assert(nif.strings.includes('Part'+String(i).padStart(2,'0')),'missing Windstep animation part '+i);
   assert(nif.blocks.filter(b=>b.type==='BSTriShape').every(b=>b.skin===-1),'windstep echo must be unskinned effect geometry');
   for(let i=0;i<3;i++){
    const echo=nif.blocks.filter(b=>b.type==='BSTriShape'&&nif.strings[b.name].startsWith('Echo'+i+'_'));
    assert(echo.length===9,'incomplete stock archer');
    assert(echo.some(b=>nif.strings[b.name].endsWith('NockedArrow')),'missing nocked arrow');
   }
  }
  if(/^ArcaneSurge\d*\.nif$/.test(e.name)){
   assert(nifio.write(nif).equals(bytes),'Arcane Surge NIF roundtrip mismatch');
   for(let i=0;i<30;i++)assert(nif.strings.includes('Surge'+String(i).padStart(2,'0')),'missing Surge part '+i);
   for(const b of nif.blocks.filter(b=>b.type==='BSTriShape'&&['Arcane2','Arcane4'].includes(nif.strings[b.name]))){
    const top=Math.max(...b.verts.map(v=>v.pos[2]));
    assert(b.verts.filter(v=>v.pos[2]>top*.999).every(v=>v.c[3]===0),'rim must fade to transparent at its top');
   }
  }
  if(file.includes(path.sep+'Support'+path.sep)){
   assert(nifio.write(nif).equals(bytes),'NIF read/write mismatch');
   for(const name of ['FWM_Core','FWM_Wisps'])assert(nif.strings.includes(name),'missing animated node '+name);
   for(const b of nif.blocks)if(b.type==='BSTriShape')for(const v of b.verts)
    assert(Math.hypot(...v.pos.map((x,k)=>x-b.center[k]))<=b.radius+.01,'stale bounds '+file);
  }
  meshes.push({path:path.relative(root,file).split(path.sep).join('/'),textures:[...textures]});
 }
}}
walk(path.join(root,'meshes'));
process.stdout.write(JSON.stringify(meshes));
