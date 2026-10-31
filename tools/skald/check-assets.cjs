// Read the shipped binary geometry and timing data, not the generator's in-memory objects.
const fs=require('fs'),path=require('path'),assert=require('node:assert/strict');
const nif=require('../wolfhead/nifio.js');
const root=path.resolve(__dirname,'../..');
const names=['SkaldFervor','SkaldEchoes','SkaldHollow','SkaldGlory',...Array.from({length:4},(_,i)=>'SkaldHit'+i),...Array.from({length:4},(_,i)=>'SkaldLute'+i)];
const policy=fs.readFileSync(path.join(root,'include/FWM/SkaldPolicy.h'),'utf8');
const number=name=>Number(policy.match(new RegExp(name+' = ([\\d.]+)f?;'))[1]);
const {families}=require('./ParticleTypes.cjs');
const geometrySets=[];
require('./check-native-ljodhs.cjs').check();
function finite(value,label){
 if(typeof value==='number')assert(Number.isFinite(value),label+' is not finite');
 else if(value&&typeof value==='object'&&!Buffer.isBuffer(value))for(const [key,child] of Object.entries(value))finite(child,label+'.'+key);
}
for(const name of names){
 const data=nif.read(fs.readFileSync(path.join(root,'assets/meshes/FightWithMe',name+'.nif')));
 const instrument=name.startsWith('SkaldLute'),hit=name.startsWith('SkaldHit');
 if(instrument){
  for(const label of ['Instrument','LuteVeil','SpectralStrings','SpectralVeil'])assert(data.strings.includes(label),name+': missing spectral layer '+label);
 }else{
  const song=hit?Number(name.at(-1)):['SkaldFervor','SkaldEchoes','SkaldHollow','SkaldGlory'].indexOf(name);
  for(const family of families[song])assert(data.strings.some(s=>s.startsWith(family.replaceAll(' ',''))),name+': missing particle family '+family);
  assert(!data.strings.some(s=>/^(Pulse|Harmonics|Runes|Wave\d|Wake\d|Spark\d|Contact\d|HitAura|HitVeil|Verse|Soundwave|Undertone)/.test(s)),name+': retired ring geometry is still present');
  const pools=hit?[['HitMote',number('kHitMotes')*number('kHitBursts')]]:[['BardMote',number('kBardMotes')],['WaveMote',number('kWaveSlots')*number('kWaveMotes')]];
  for(const [prefix,count] of pools)for(let i=0;i<count;i++)assert(data.blocks.some(b=>b.type==='NiBillboardNode'&&data.strings[b.name]===prefix+'Facing'+i),name+': missing particle cluster '+i);
  for(const b of data.blocks.filter(b=>b.type==='BSTriShape'))assert(b.tris.length>=10,name+': particle density must use clustered specks');
  for(const [prefix] of pools){
   const shapes=Array.from({length:4},(_,i)=>data.blocks.find(b=>b.type==='BSTriShape'&&data.strings[b.name]===prefix+'Glow'+(i+4)));
   const signatures=shapes.map(b=>JSON.stringify([b.verts.map(v=>v.pos),b.tris]));
   assert.equal(new Set(signatures).size,4,name+': rank I must contain four different silhouettes');
   assert(data.strings.includes('SpiritDust0'),name+': preserve the luminous dust base');
   if(!hit&&prefix==='BardMote')geometrySets.push(signatures.join('|'));
  }
 }
 let vertices=0,triangles=0;
 for(const b of data.blocks){
  finite(b,name+'/'+b.type);
  if(b.type==='BSTriShape'){
   const shader=data.blocks[b.shader];
   const label=data.strings[b.name];
   if(label==='Verse'||label.startsWith('Soundwave')||label.startsWith('Undertone')||label.startsWith('Contact')){
    assert.equal(shader.flags1&0x40000000,0,name+': floor effect must not fade into terrain');
    assert.equal(shader.flags1&0x40,0,name+': floor effect must remain visible at oblique angles');
   }
   if(shader.source.includes('\\Skald\\')){
    const file=path.join(root,'assets',...shader.source.split('\\'));assert(fs.existsSync(file),'Missing painted texture '+file);
    const dds=fs.readFileSync(file);assert.equal(dds.toString('ascii',0,4),'DDS ');assert.equal(dds.toString('ascii',84,88),'DXT5');
    assert.equal(dds.readUInt32LE(12),1024);assert.equal(dds.readUInt32LE(16),1024);assert.equal(dds.readUInt32LE(28),11);
    for(const v of b.verts)for(const u of v.uv.map(nif.half))assert(u>=0&&u<=1,'Atlas UV outside texture');
   }
   assert(b.verts.length>0 && b.tris.length>0,name+': empty shape');
   for(const tri of b.tris)for(const index of tri)assert(Number.isInteger(index)&&index>=0&&index<b.verts.length,name+': bad triangle index');
   if(hit)for(const v of b.verts)assert(Math.hypot(v.pos[0],v.pos[1])<=number('kHitRadius')+.001,'Hit marker must stay small');
   vertices+=b.verts.length;triangles+=b.tris.length;
  }
 }
 assert(vertices>0&&triangles>0,name+': no geometry');
 console.log(name+': '+vertices+' vertices, '+triangles+' triangles, finite');
}
assert.equal(new Set(geometrySets).size,4,'The four songs must not ship identical particle geometry');
const tracks=JSON.parse(fs.readFileSync(path.join(__dirname,'beat-map.json'),'utf8'));
const header=fs.readFileSync(path.join(root,'include/FWM/SkaldMusic.h'),'utf8');
assert.equal(tracks.length,4);
for(const [i,track] of tracks.entries()){
 assert(/^[a-f0-9]{64}$/.test(track.sha256));
 assert.equal(require('crypto').createHash('sha256').update(fs.readFileSync(path.join(root,'assets/sound/fx/FightWithMe/Skald',track.file))).digest('hex'),track.sha256);
 assert.equal(track.beats.length,20);
 assert.equal(track.waves.length,10);
 assert.equal(track.beatsPerWave,2);
 assert(track.bpm>=90&&track.bpm<=160);
 assert(track.seconds>8 && track.seconds<11);
 assert(header.toLowerCase().includes(track.file.toLowerCase()));
 track.beats.forEach((beat,n)=>assert(Number.isFinite(beat)&&beat>0&&beat<track.seconds&&(!n||beat>track.beats[n-1]),'Invalid pulse time'));
 assert(header.includes('beats'+i+'{'+track.beats.map(n=>n.toFixed(3)+'f').join(',')+'}'),'Header and measured pulses differ');
 assert(header.includes('waves'+i+'{'+track.waves.map(n=>n.toFixed(3)+'f').join(',')+'}'),'Header and tempo waves differ');
 track.waves.forEach((t,j)=>{assert.equal(t,track.beats[j*2]);if(j)assert(Math.abs(t-track.waves[j-1]-120/track.bpm)<.0011,'Waves must keep the tempo');});
 const life=number('kReach')/number('kWaveSpeed');
 for(let t=0;t<track.seconds+number('kHitParticleSeconds');t+=.02)assert(track.waves.filter(b=>t>=b&&t-b<number('kHitParticleSeconds')).length<=number('kHitBursts'),'Recipient particle pool too small for tempo');
 for(let t=0;t<track.seconds+life;t+=.01)assert(track.waves.filter(b=>t>=b&&t-b<life).length<=2,'Too many simultaneous waves');
 console.log(track.file+': '+track.bpm+' BPM, 10 waves, maximum two traveling particle waves');
}
for(const theme of ['vanilla','oxblood','nordic']){
 const b=fs.readFileSync(path.join(root,'assets/interface/FightWithMe/backdrops',theme+'-skald.png'));
 assert.equal(b.subarray(0,8).toString('hex'),'89504e470d0a1a0a');
 assert(b.readUInt32BE(16)>=1024&&b.readUInt32BE(20)>=1024);
 assert.equal(b[25],6,'Portrait must retain RGBA transparency');
}
const note=fs.readFileSync(path.join(root,'assets/sound/fx/FightWithMe/Skald/SkaldNote.wav'));
const provenance=JSON.parse(fs.readFileSync(path.join(__dirname,'note-audio.json'),'utf8'));
assert.equal(note.toString('ascii',0,4),'RIFF');assert.equal(note.toString('ascii',8,12),'WAVE');
assert.equal(note.readUInt16LE(20),1);assert.equal(note.readUInt16LE(22),1);assert.equal(note.readUInt16LE(34),16);
assert.equal(require('crypto').createHash('sha256').update(note).digest('hex'),provenance.output_sha256);
assert(provenance.seconds>=.65&&provenance.seconds<=1);
console.log('Skald asset checks passed: 12 meshes, 4 tempo maps, single-note audio, 3 transparent portraits.');
