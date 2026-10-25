// Gathers what the ability preview shows, from the same sources the game uses:
//   out/head.json, out/head-plain.json  the two head meshes, read back from assets/meshes/FightWithMe
//   out/pose.json                       the bite, sampled by FightWithMeWolfPose (the plugin's own code)
//   out/circle.json, out/blast-*.json   the healer's circle and Elemental Burst's three blasts
//   out/circle-look.json, blast-look.json  their frames, from FightWithMeCircleLook and FightWithMeBlastLook
//   out/textures/*.png                  every texture the heads use, as the game would load it: the
//                                       winning loose file in the MO2 profile's mod order, else the
//                                       vanilla archives; converted with texconv
//
//   node tools\wolfhead\preview\build.js
//
// Paths are this machine's; change them below for another install.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { execFileSync } = require('child_process');
const nifio = require('../nifio.js');
const { open } = require('../bsa.js');

const REPO = path.resolve(__dirname, '..', '..', '..');
const NOLVUS = 'X:/!--- Main/Documents/Nolvus';
const PROFILE = `${NOLVUS}/MODS/profiles/Nolvus Awakening/modlist.txt`;
const MODS = `${NOLVUS}/MODS/mods`;
const OVERWRITE = `${NOLVUS}/MODS/overwrite`;
const VANILLA = `${NOLVUS}/STOCK GAME/Data`;
const TEXCONV = `${NOLVUS}/TOOLS/SSE Edit/Edit Scripts/Texconvx64.exe`;
const POSE = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeWolfPose.exe');
const OUT = path.join(__dirname, 'out');
const STOCK_TEXTURES = process.argv.includes('--stock-textures');

fs.mkdirSync(path.join(OUT, 'textures'), { recursive: true });

// --- Textures, resolved as the game would ---------------------------------------------------

const mods = fs.readFileSync(PROFILE, 'utf8').split(/\r?\n/).filter(l => l.startsWith('+')).map(l => l.slice(1));
const vanilla = [];
for (let i = 0; i < 9; i++) {
  const file = `${VANILLA}/Skyrim - Textures${i}.bsa`;
  if (fs.existsSync(file)) vanilla.push(open(file));
}

function resolve(texture) {
  const relative = texture.replace(/\\/g, '/');
  // Fight With Me's own textures first, as they will be once deployed.
  for (const root of [path.join(REPO, 'assets'), ...(STOCK_TEXTURES ? [] : [OVERWRITE, ...mods.map(m => `${MODS}/${m}`)])]) {
    const file = `${root}/${relative}`;
    if (fs.existsSync(file)) return { from: root.endsWith('assets') ? 'Fight With Me' : path.basename(root), bytes: fs.readFileSync(file) };
  }
  for (const bsa of vanilla) {
    const hit = bsa.files.find(f => f.path.toLowerCase() === texture.toLowerCase());
    if (hit) return { from: 'vanilla', bytes: bsa.read(hit) };
  }
  return null;
}

const converted = new Map();
function png(texture) {
  if (!texture) return null;
  if (converted.has(texture)) return converted.get(texture);
  const found = resolve(texture);
  if (!found) { console.log(`  missing: ${texture}`); converted.set(texture, null); return null; }
  const id = crypto.createHash('sha1').update(texture.toLowerCase()).digest('hex').slice(0, 12);
  const dds = path.join(OUT, 'textures', `${id}.dds`);
  fs.writeFileSync(dds, found.bytes);
  execFileSync(TEXCONV, ['-nologo', '-y', '-ft', 'png', '-f', 'R8G8B8A8_UNORM', '-m', '1', '-o', path.join(OUT, 'textures'), dds], { stdio: 'pipe' });
  fs.unlinkSync(dds);
  const name = `textures/${id}.png`;
  console.log(`  ${texture} <- ${found.from}`);
  converted.set(texture, name);
  return name;
}


function compactSamples() {
  for(const [exe,file,args] of [
    ['BreathLook','breath-look-compact.json',['compact']],
    ['BearLook','bear-look-compact.json',['compact']],
    ['WolfPose','pose-compact.json',['70','60','compact']],
    ['MoonLook','azura-look-compact.json',['compact']]])
      fs.writeFileSync(path.join(OUT,file),execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMe'+exe+'.exe'),args,{maxBuffer:64*1024*1024}));
  const runs={};for(const seconds of [8,10,12,14])runs[seconds]=JSON.parse(execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeBastionLook.exe'),[String(seconds),'compact'],{maxBuffer:32*1024*1024}));
  fs.writeFileSync(path.join(OUT,'bastion-look-compact.json'),JSON.stringify({...runs[8],runs}));
}


// --- Heads -----------------------------------------------------------------------------------

const unpack = b => b / 255 * 2 - 1;
if(process.argv.includes('--compact-only')) {compactSamples();breathAssets();head(path.join(REPO,'assets/meshes/FightWithMe/ArrowRain.nif'),'rain.json');fs.writeFileSync(path.join(OUT,'rain-look.json'),execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeArrowRainLook.exe'),['60'],{maxBuffer:64*1024*1024}));process.exit(0);}
function head(nifFile, outFile) {
  const nif = nifio.read(fs.readFileSync(nifFile));
  const name = i => (i >= 0 ? nif.strings[i] : '');
  const parentOf = new Map();
  nif.blocks.forEach((b, i) => { if (b.children) for (const c of b.children) parentOf.set(c, i); });
  const jaw = nif.blocks.find(b => b.type === 'NiNode' && name(b.name) === 'Jaw');
  const shapes = [];
  nif.blocks.forEach((b, i) => {
    if (b.type !== 'BSTriShape') return;
    const shader = nif.blocks[b.shader];
    const alpha = b.alpha >= 0 ? nif.blocks[b.alpha] : null;
    const effect = shader.type === 'BSEffectShaderProperty';
    const textures = effect ? [shader.source, ''] : nif.blocks[shader.textureSet].textures;
    const round = x => Math.round(x * 1000) / 1000;
    shapes.push({
      name: name(b.name),
      node: name(nif.blocks[parentOf.get(i)].name),
      // The game's alpha test runs only when its bit is set; blending, when that one is.
      threshold: alpha && (alpha.alphaFlags & 0x200) ? alpha.threshold : 0,
      additive: !!alpha && (alpha.alphaFlags & 1) !== 0 && ((alpha.alphaFlags >> 5) & 0xf) === 0,
      doubleSided: (shader.flags2 & 0x10) !== 0,
      source: textures[0],
      diffuse: png(textures[0]),
      normal: png(textures[1]),
      effect: effect ? {
        palette: shader.flags1 & 0x10 ? png(shader.greyscale) : null,
        color: shader.color, scale: shader.colorScale,
        falloff: shader.flags1 & 0x40 ? shader.falloff : null,
        uvScale: shader.uvScale, uvOffset: shader.uvOffset
      } : null,
      positions: b.verts.flatMap(v => v.pos.map(round)),
      normals: b.verts.flatMap(v => { const n = v.n.map(unpack); const l = Math.hypot(...n) || 1; return n.map(x => round(x / l)); }),
      uvs: b.verts.flatMap(v => v.uv.map(h => round(nifio.half(h)))),
      colors: b.verts.flatMap(v => v.c.map(x => round(x / 255))),
      indices: b.tris.flat(),
      transform: { translate: b.translate, rotate: b.rotate, scale: b.scale }  // the shape's own, under its node
    });
  });
  // Every node, for meshes whose parts move on their own (the circle's runes and spikes).
  const nodes = nif.blocks.map((b, i) => (b.type === 'NiNode' || b.type === 'BSFadeNode' || b.type === 'NiBillboardNode'
    ? { name: name(b.name), parent: parentOf.has(i) ? name(nif.blocks[parentOf.get(i)].name) : null, translate: b.translate, billboard: b.type === 'NiBillboardNode' } : null)).filter(Boolean);
  fs.writeFileSync(path.join(OUT, outFile), JSON.stringify({ jaw: jaw ? jaw.translate : [0, 0, 0], nodes, shapes }));
  console.log(`${outFile}: ${shapes.length} shapes`);
}

function beastAssets() {
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralBearHead.nif'), 'bear.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralBearHeadPlain.nif'), 'bear-plain.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralBearHeadRed.nif'), 'bear-red.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralBearHeadRedPlain.nif'), 'bear-red-plain.json');
const BEAR = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeBearLook.exe');
fs.writeFileSync(path.join(OUT, 'bear-look.json'), execFileSync(BEAR));
// Preview only: reference the same bear roar as Skyrim.esm 000F95BC. Never packaged.
const sounds = open(path.join(VANILLA, 'Skyrim - Sounds.bsa'));
const roar = sounds.files.find(f => f.path.toLowerCase() === 'sound\\fx\\npc\\bear\\npc_bear_aggrowarning_02.wav');
if (!roar) throw new Error('Taunt preview: bear roar missing from Skyrim - Sounds.bsa');
fs.writeFileSync(path.join(OUT, 'bear-roar.wav'), sounds.read(roar));
for (const [mesh, name] of [['WarCrySabre','warcry-sabre'],['WarCrySabreRed','warcry-sabre-red']]) head(path.join(REPO, 'assets/meshes/FightWithMe',mesh+'.nif'),name+'.json');
}
function breathAssets() {
  for(const [rank,tier] of ['','II','III'].entries())for(const element of ['Fire','Frost','Shock'])head(path.join(REPO,'assets/meshes/FightWithMe','Breath'+element+tier+'Compact.nif'),'breath-'+element.toLowerCase()+'-'+(rank+1)+'-compact.json');
  for(const [mesh,name] of [['DragonBreathHead','breath-head'],['DragonBreathAlduin','breath-alduin']])head(path.join(REPO,'assets/meshes/FightWithMe',mesh+'.nif'),name+'.json');
  for(const [rank,tier] of ['','II','III'].entries())for(const element of ['Fire','Frost','Shock'])head(path.join(REPO,'assets/meshes/FightWithMe','Breath'+element+tier+'.nif'),'breath-'+element.toLowerCase()+'-'+(rank+1)+'.json');
  fs.writeFileSync(path.join(OUT,'breath-look.json'),execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeBreathLook.exe'),{maxBuffer:16*1024*1024}));
}
if (process.argv.includes('--breath-only')) { breathAssets(); process.exit(0); }
function surgeAssets() {
  head(path.join(REPO,'assets/meshes/FightWithMe/ArcaneSurge.nif'),'surge.json');
  head(path.join(REPO,'assets/meshes/FightWithMe/Support/Surge.nif'),'support-Surge.json');
  fs.writeFileSync(path.join(OUT,'surge-look.json'),execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeArcaneSurgeLook.exe'),{maxBuffer:32*1024*1024}));
  fs.writeFileSync(path.join(OUT,'support-look.json'),execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeSupportLook.exe'),{maxBuffer:32*1024*1024}));
}
if (process.argv.includes('--surge-only')) {surgeAssets();process.exit(0);}
if (process.argv.includes('--berserk-only')) {
  head(path.join(REPO,'assets/meshes/FightWithMe/BerserkMantle.nif'),'berserk.json');
  const frames=execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeBerserkLook.exe'),{maxBuffer:32*1024*1024});
  fs.writeFileSync(path.join(OUT,'berserk-look.json'),frames);
  const data=JSON.parse(fs.readFileSync(path.join(OUT,'berserk.json'))),look=JSON.parse(frames);
  const names=new Set(data.nodes.map(n=>n.name));
  for(const name of look.names)if(!names.has(name))throw Error('Berserk node missing: '+name);
  if(data.shapes.some(s=>!s.diffuse))throw Error('Berserk texture missing');
  process.exit(0);
}
if (process.argv.includes('--spell-arrows-only')) {
  for (const name of ['Fire','Frost','Shock']) head(path.join(REPO,'assets/meshes/FightWithMe/SpellArrow'+name+'.nif'),'spell-arrow-'+name.toLowerCase()+'.json');
  process.exit(0);
}
if (process.argv.includes("--bear-only")) { beastAssets(); process.exit(0); }

// A focused refresh for the standalone supporting art and replacement fur.
if (process.argv.includes('--support-only')) {
  const folder = path.join(REPO,'assets/meshes/FightWithMe/Support');
  for (const file of fs.readdirSync(folder).filter(f=>f.endsWith('.nif'))) head(path.join(folder,file),'support-'+file.replace('.nif','.json'));
  for (const [file,name] of [['SpectralWolfHead','head'],['SpectralWolfHeadRed','head-red'],['SpectralBearHead','bear'],['SpectralBearHeadRed','bear-red']])
    head(path.join(REPO,'assets/meshes/FightWithMe',file+'.nif'),name+'.json');
  fs.writeFileSync(path.join(OUT,'support-look.json'), execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeSupportLook.exe'), {maxBuffer:32*1024*1024}));
  process.exit(0);
}

function rejuvenationAssets() {
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'RejuvenationCircle.nif'), 'rejuvenation.json');
  const sample = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeRejuvenationLook.exe');
  const look = JSON.parse(execFileSync(sample, { encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 }));
  const mesh = JSON.parse(fs.readFileSync(path.join(OUT, 'rejuvenation.json'), 'utf8'));
  const names = [...look.names, ...Array.from({ length: 72 }, (_, i) => `Mote${String(i).padStart(2, '0')}`)];
  for (const name of names) if (!mesh.nodes.some(n => n.name === name)) throw new Error(`Rejuvenation is missing ${name}`);
  fs.writeFileSync(path.join(OUT, 'rejuvenation-look.json'), JSON.stringify(look));
}

function weakWillAssets() {
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'WeakWillHelmet.nif'), 'weakwill.json');
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'WeakWillHelmetGrand.nif'), 'weakwill-grand.json');
  const sample = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeWeakWillLook.exe');
  const runs = {};
  for (const seconds of [8, 10, 12, 14, 16, 18]) runs[seconds] = JSON.parse(execFileSync(sample, [String(seconds)], { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }));
  const look = { ...runs[10], runs };
  for (const file of ['weakwill.json', 'weakwill-grand.json']) {
    const mesh = JSON.parse(fs.readFileSync(path.join(OUT, file), 'utf8'));
    for (const name of look.names) if (!mesh.nodes.some(n => n.name === name)) throw new Error(`Weak Will is missing ${name}`);
  }
  fs.writeFileSync(path.join(OUT, 'weakwill-look.json'), JSON.stringify(look));
}

function bastionAssets() {
  head(path.join(REPO, 'assets/meshes/FightWithMe/LastBastionTower.nif'), 'bastion.json');
  head(path.join(REPO, 'assets/meshes/FightWithMe/LastBastionCastle.nif'), 'bastion-grand.json');
  const sample = path.join(REPO, 'build/skyrim/Release/FightWithMeBastionLook.exe');
  const runs = {};
  for (const seconds of [8, 10, 12, 14]) runs[seconds] = JSON.parse(execFileSync(sample, [String(seconds)], {encoding:'utf8',maxBuffer:32*1024*1024}));
  const look = {...runs[8],runs};
  for (const file of ['bastion.json','bastion-grand.json']) {
    const mesh=JSON.parse(fs.readFileSync(path.join(OUT,file),'utf8'));
    for(const name of look.names) if(!mesh.nodes.some(n=>n.name===name))throw new Error(`Last Bastion missing ${name}`);
  }
  fs.writeFileSync(path.join(OUT,'bastion-look.json'),JSON.stringify(look));
  require('./audio-assets').buildAudio(REPO,VANILLA,OUT,look);
}

function sunderAssets() {
  const metal = path.join(REPO, 'assets', 'textures', 'FightWithMe', 'SunderMetal.dds');
  if (!fs.existsSync(metal)) throw new Error('Sunder is missing assets/textures/FightWithMe/SunderMetal.dds');
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SunderBreak.nif'), 'sunder.json');
  const sample = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeSunderLook.exe');
  const look = JSON.parse(execFileSync(sample, ['60'], { encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 }));
  const mesh = JSON.parse(fs.readFileSync(path.join(OUT, 'sunder.json'), 'utf8'));
  const names = new Set(mesh.nodes.map(n => n.name));
  for (const name of ['Sunder', 'Mark', 'Burst', ...look.parts]) {
    if (!names.has(name)) throw new Error('Sunder is missing ' + name);
  }
  for (const rank of ['plain', 'grand']) {
    if (!look[rank].frames.length || look[rank].frames.some(f => f.parts.length !== look.parts.length)) {
      throw new Error('Sunder ' + rank + ' frames do not match the mesh parts');
    }
  }
  fs.writeFileSync(path.join(OUT, 'sunder-look.json'), JSON.stringify(look));
}

function windstepAssets(){
 head(path.join(REPO,'assets/meshes/FightWithMe/WindstepFemale.nif'),'windstep-female.json');
 head(path.join(REPO,'assets/meshes/FightWithMe/Windstep.nif'),'windstep.json');
 const look=JSON.parse(execFileSync(path.join(REPO,'build/skyrim/Release/FightWithMeWindstepLook.exe'),[],{encoding:'utf8',maxBuffer:24*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'windstep.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('Windstep missing '+name);
 fs.writeFileSync(path.join(OUT,'windstep-look.json'),JSON.stringify(look));
}
if(process.argv.includes('--windstep-only')){windstepAssets();process.exit(0);}
function arrowartsAssets(){
 fs.writeFileSync(path.join(OUT,'arrow-explosion.json'),JSON.stringify({fire:png('textures\\effects\\FXfireAnim04loop.dds'),glow:png('textures\\effects\\FXGlowSpotLinear.dds')}));
 head(path.join(REPO,'assets/meshes/FightWithMe/ArrowArts.nif'),'arrowarts.json');
 const sample=path.join(REPO,'build/skyrim/Release/FightWithMeArrowArtsLook.exe');
 const look=JSON.parse(execFileSync(sample,[],{encoding:'utf8',maxBuffer:24*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'arrowarts.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('ArrowArts missing '+name);
 fs.writeFileSync(path.join(OUT,'arrowarts-look.json'),JSON.stringify(look));
}
if(process.argv.includes('--arrowarts-only')){arrowartsAssets();process.exit(0);}
function riposteAssets(){
 head(path.join(REPO,'assets/meshes/FightWithMe/RiposteStrike.nif'),'riposte.json');
 const sample=path.join(REPO,'build/skyrim/Release/FightWithMeRiposteLook.exe');
 const look=JSON.parse(execFileSync(sample,[],{encoding:'utf8',maxBuffer:24*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'riposte.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('Riposte missing '+name);
 fs.writeFileSync(path.join(OUT,'riposte-look.json'),JSON.stringify(look));
}
if(process.argv.includes('--riposte-only')){riposteAssets();process.exit(0);}
function disarmAssets(){
 head(path.join(REPO,'assets/meshes/FightWithMe/DisarmStrike.nif'),'disarm.json');
 const sample=path.join(REPO,'build/skyrim/Release/FightWithMeDisarmLook.exe');
 const look=JSON.parse(execFileSync(sample,[],{encoding:'utf8',maxBuffer:16*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'disarm.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('Disarm missing '+name);
 fs.writeFileSync(path.join(OUT,'disarm-look.json'),JSON.stringify(look));
}
if(process.argv.includes('--disarm-only')){disarmAssets();process.exit(0);}
function secondwindAssets(){
 head(path.join(REPO,'assets/meshes/FightWithMe/SecondWind.nif'),'secondwind.json');
 const sample=path.join(REPO,'build/skyrim/Release/FightWithMeSecondWindLook.exe');
 const look=JSON.parse(execFileSync(sample,[],{encoding:'utf8',maxBuffer:16*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'secondwind.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('SecondWind missing '+name);
 fs.writeFileSync(path.join(OUT,'secondwind-look.json'),JSON.stringify(look));
}
function moonAssets(){
 head(path.join(REPO,'assets/meshes/FightWithMe/AzurasMoon.nif'),'azura.json');
 const sample=path.join(REPO,'build/skyrim/Release/FightWithMeMoonLook.exe');
 const look=JSON.parse(execFileSync(sample,[],{encoding:'utf8',maxBuffer:40*1024*1024}));
 const mesh=JSON.parse(fs.readFileSync(path.join(OUT,'azura.json'),'utf8'));
 for(const name of look.names)if(!mesh.nodes.some(n=>n.name===name))throw Error('Azura missing '+name);
 fs.writeFileSync(path.join(OUT,'azura-look.json'),JSON.stringify(look));
}
if(process.argv.includes('--moon-only')){moonAssets();process.exit(0);}
if(process.argv.includes('--secondwind-only')){secondwindAssets();process.exit(0);}
console.log('textures:');
if (process.argv.includes('--tank-only')) { weakWillAssets(); bastionAssets(); process.exit(0); }
if (process.argv.includes('--bastion-only')) { bastionAssets(); process.exit(0); }
if (process.argv.includes('--weakwill-only')) { weakWillAssets(); process.exit(0); }
if (process.argv.includes('--sunder-only')) { sunderAssets(); process.exit(0); }
// Refresh only Elemental Burst while iterating its mesh/materials. Runtime samplers
// remain the source of timing and node names, just as in the complete build below.
if (process.argv.includes('--blast-only')) {
  for (const element of ['Fire', 'Frost', 'Shock']) {
    head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', `ElementalBlast${element}.nif`), `blast-${element.toLowerCase()}.json`);
  }
  const sample = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeBlastLook.exe');
  const look = JSON.parse(execFileSync(sample, ['60'], { maxBuffer: 64 * 1024 * 1024 }));
  look.elements.forEach((element, i) => {
    const file = `blast-${['fire', 'frost', 'shock'][i]}.json`;
    const nodes = new Set(JSON.parse(fs.readFileSync(path.join(OUT, file), 'utf8')).nodes.map(n => n.name));
    const missing = ['Orb', 'Blast', ...element.parts].filter(n => !nodes.has(n));
    if (missing.length) throw new Error(`${file} lacks the nodes ${missing.join(', ')}`);
  });
  fs.writeFileSync(path.join(OUT, 'blast-look.json'), JSON.stringify(look));
  process.exit(0);
}
if (process.argv.includes('--magic-only')) {
  rejuvenationAssets();
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'HealingCircle.nif'), 'circle.json');
  fs.writeFileSync(path.join(OUT, 'circle-look.json'), execFileSync(path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeCircleLook.exe'), ['30'], { maxBuffer: 16 * 1024 * 1024 }));
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'MagnusOrb.nif'), 'magnus.json');
  fs.writeFileSync(path.join(OUT, 'magnus-look.json'), execFileSync(path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeMagnusLook.exe'), ['30'], { maxBuffer: 64 * 1024 * 1024 }));
  process.exit(0);
}
breathAssets();
weakWillAssets();
bastionAssets();
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'ChaosDown.nif'), 'chaos.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'ChaosDownGrand.nif'), 'chaos-grand.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'ChaosBinding.nif'), 'chaos-binding.json');
const CHAOS = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeChaosLook.exe');
const chaosLook = JSON.parse(execFileSync(CHAOS, { encoding: 'utf8', maxBuffer: 8 * 1024 * 1024 }));
const chaosMesh = JSON.parse(fs.readFileSync(path.join(OUT, 'chaos.json'), 'utf8'));
for (const name of [...chaosLook.names, 'Spirit', ...chaosLook.joints]) if (!chaosMesh.nodes.some(n => n.name === name)) throw new Error(`Chaos Down is missing ${name}`);
const chaosGrandMesh = JSON.parse(fs.readFileSync(path.join(OUT, 'chaos-grand.json'), 'utf8'));
for (const name of [...chaosLook.names, 'Spirit', ...chaosLook.joints]) if (!chaosGrandMesh.nodes.some(n => n.name === name)) throw new Error(`Chaos Down III is missing ${name}`);
if (!chaosLook.binding) throw new Error('Chaos binding sampler is missing: rebuild FightWithMeChaosLook');
const bindingMesh = JSON.parse(fs.readFileSync(path.join(OUT, 'chaos-binding.json'), 'utf8'));
for (const name of chaosLook.binding.names) if (!bindingMesh.nodes.some(n => n.name === name)) throw new Error('Chaos binding is missing ' + name);
// Each supported Lingering rank is sampled with its real remaining time: no repeated or
// stretched swirl, and the actual final fade matches Skyrim for every duration.
chaosLook.binding.runs = {};
chaosLook.runs = {};
for (const seconds of [10, 12, 14]) {
  const full = JSON.parse(execFileSync(CHAOS, [String(seconds)], { encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 }));
  chaosLook.runs[seconds] = { seconds:full.seconds, frames:full.frames, grand:full.grand };
  const sampled = full.binding;
  if (!sampled || Math.abs(sampled.seconds - seconds) > 0.01) throw new Error('Chaos binding sampler has not been rebuilt for variable duration');
  chaosLook.binding.runs[seconds] = { seconds: sampled.seconds, frames: sampled.frames, grand: sampled.grand };
}
fs.writeFileSync(path.join(OUT, 'chaos-look.json'), JSON.stringify(chaosLook));
if (process.argv.includes('--chaos-only')) process.exit(0);
beastAssets();
rejuvenationAssets();
sunderAssets();
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralWolfHead.nif'), 'head.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralWolfHeadPlain.nif'), 'head-plain.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralWolfHeadRed.nif'), 'head-red.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'SpectralWolfHeadRedPlain.nif'), 'head-red-plain.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'Haunt.nif'), 'haunt.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'HealingCircle.nif'), 'circle.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'MagnusOrb.nif'), 'magnus.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'Earthshaker.nif'), 'quake.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'ArrowRain.nif'), 'rain.json');
head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', 'WaterCloak.nif'), 'cloak.json');
for (const element of ['Fire', 'Frost', 'Shock']) {
  head(path.join(REPO, 'assets', 'meshes', 'FightWithMe', `ElementalBlast${element}.nif`), `blast-${element.toLowerCase()}.json`);
}

// --- The bite, from the plugin's own code ---------------------------------------------------

if (!fs.existsSync(POSE)) throw new Error(`build the FightWithMeWolfPose target first (${POSE})`);
fs.writeFileSync(path.join(OUT, 'pose.json'), execFileSync(POSE, ['70', '60'], { maxBuffer: 64 * 1024 * 1024 }));
console.log('pose.json written');

// The healer's circle, from the plugin's own code too.
const LOOK = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeCircleLook.exe');
if (!fs.existsSync(LOOK)) throw new Error(`build the FightWithMeCircleLook target first (${LOOK})`);
fs.writeFileSync(path.join(OUT, 'circle-look.json'), execFileSync(LOOK, ['30']));
console.log('circle-look.json written');

// Elemental Burst's blasts, likewise; every part the plugin poses must be a node in its mesh.
const BLAST = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeBlastLook.exe');
if (!fs.existsSync(BLAST)) throw new Error(`build the FightWithMeBlastLook target first (${BLAST})`);
const blastLook = execFileSync(BLAST, ['60'], { maxBuffer: 64 * 1024 * 1024 });
fs.writeFileSync(path.join(OUT, 'blast-look.json'), blastLook);
JSON.parse(blastLook).elements.forEach((element, i) => {
  const file = `blast-${['fire', 'frost', 'shock'][i]}.json`;
  const nodes = new Set(JSON.parse(fs.readFileSync(path.join(OUT, file), 'utf8')).nodes.map(n => n.name));
  const missing = ['Orb', 'Blast', ...element.parts].filter(n => !nodes.has(n));
  if (missing.length) throw new Error(`${file} lacks the nodes ${missing.join(', ')}`);
});
console.log('blast-look.json written');

// Magnus's orb, plain (its first rank, twelve seconds) and grand (its third, twenty-five).
const MAGNUS = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeMagnusLook.exe');
if (!fs.existsSync(MAGNUS)) throw new Error(`build the FightWithMeMagnusLook target first (${MAGNUS})`);
fs.writeFileSync(path.join(OUT, 'magnus-look.json'), execFileSync(MAGNUS, ['30'], { maxBuffer: 64 * 1024 * 1024 }));
console.log('magnus-look.json written');

// Earthshaker's slam, plain (ranks I and II) and grand (rank III).
const QUAKE = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeQuakeLook.exe');
if (!fs.existsSync(QUAKE)) throw new Error(`build the FightWithMeQuakeLook target first (${QUAKE})`);
fs.writeFileSync(path.join(OUT, 'quake-look.json'), execFileSync(QUAKE, ['30'], { maxBuffer: 64 * 1024 * 1024 }));
console.log('quake-look.json written');

// Arrow Rain, a plain rain of 12 stand-in arrows and a grand one of 28; every node the plugin poses must be in its mesh.
const RAIN = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeArrowRainLook.exe');
if (!fs.existsSync(RAIN)) throw new Error(`build the FightWithMeArrowRainLook target first (${RAIN})`);
fs.writeFileSync(path.join(OUT, 'rain-look.json'), execFileSync(RAIN, ['60'], { maxBuffer: 64 * 1024 * 1024 }));
{
  const nodes = new Set(JSON.parse(fs.readFileSync(path.join(OUT, 'rain.json'), 'utf8')).nodes.map(n => n.name));
  const two = k => String(k).padStart(2, '0');
  const wanted = ['Mark', 'Ring', 'Dashes', 'Heads', 'Outer', 'Floor', 'Sky', 'Halo', 'Swirl', 'Crown', 'Burst', 'Flare'];
  for (let k = 0; k < 28; k++) wanted.push(`Arrow${two(k)}`, `Arrow${two(k)}Shape`, `Arrow${two(k)}Trail`, `Arrow${two(k)}Glint`, `Hit${two(k)}`, `Hit${two(k)}Spark`, `Hit${two(k)}Ripple`, `Hit${two(k)}Dust`);
  const missing = wanted.filter(n => !nodes.has(n));
  if (missing.length) throw new Error(`rain.json lacks the nodes ${missing.join(', ')}`);
}
console.log('rain-look.json written');

// Renewal's cloak of water, twelve seconds of it with three companions washed; every node the plugin poses must be in its mesh.
const CLOAK = path.join(REPO, 'build', 'skyrim', 'Release', 'FightWithMeCloakLook.exe');
if (!fs.existsSync(CLOAK)) throw new Error(`build the FightWithMeCloakLook target first (${CLOAK})`);
fs.writeFileSync(path.join(OUT, 'cloak-look.json'), execFileSync(CLOAK, ['30'], { maxBuffer: 64 * 1024 * 1024 }));
{
  const nodes = new Set(JSON.parse(fs.readFileSync(path.join(OUT, 'cloak.json'), 'utf8')).nodes.map(n => n.name));
  const two = k => String(k).padStart(2, '0');
  const wanted = ['Cloak', 'Veil', 'Inner', 'Streams', 'Crown', 'Pool', 'Ripple0', 'Ripple1', 'Ripple2'];
  for (let k = 0; k < 10; k++) wanted.push(`Drop${two(k)}`);
  for (let k = 0; k < 8; k++) wanted.push(`Wash${two(k)}`, `Wash${two(k)}Band`, `Wash${two(k)}Ring`);
  const missing = wanted.filter(n => !nodes.has(n));
  if (missing.length) throw new Error(`cloak.json lacks the nodes ${missing.join(', ')}`);
}
console.log('cloak-look.json written');

disarmAssets();
riposteAssets();
arrowartsAssets();

secondwindAssets();
moonAssets();

windstepAssets();

surgeAssets();

compactSamples();
