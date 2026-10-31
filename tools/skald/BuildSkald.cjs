// Spectral instrument and pooled, camera-facing particle clusters; no ability rings.
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const root=path.resolve(__dirname,'../..'),out=path.join(root,'assets/meshes/FightWithMe');
const {piece,vertex,meshWriter,edged,FX,textured}=require('../fx/meshkit.js');
const nif=require('../wolfhead/nifio.js');
const policy=fs.readFileSync(path.join(root,'include/FWM/SkaldPolicy.h'),'utf8');
const constant=name=>Number(policy.match(new RegExp(name+' = ([\\d.]+)f?;'))[1]);
const source=nif.read(fs.readFileSync(path.join(root,'build/skald-research/lute.nif')));
const shape=source.blocks.find(b=>b.type==='BSTriShape');
const instrument=piece(),veil=piece();
for(const v of shape.verts){
 const r=shape.rotate,p=v.pos.map(x=>x*shape.scale),n=v.n.map(x=>x/255*2-1);
 const turn=x=>[r[0]*x[0]+r[1]*x[1]+r[2]*x[2],r[3]*x[0]+r[4]*x[1]+r[5]*x[2],r[6]*x[0]+r[7]*x[1]+r[8]*x[2]];
 const pos=turn(p).map((x,i)=>x+shape.translate[i]),normal=turn(n),uv=[v.pos[0]/20,v.pos[2]/20];
 instrument.verts.push(vertex(pos,normal,.82,1,uv));
 veil.verts.push(vertex(pos.map((x,i)=>x+normal[i]*1.2),normal,.35,1,uv));
}
instrument.tris=veil.tris=shape.tris;
for(let copy=0;copy<4;copy++){
 const w=meshWriter();
 const body=w.shape('SpectralStrings',instrument,textured({...edged(3.1,.6),hard:true},FX.cloud,[1.5,1.5]));
 const shell=w.shape('SpectralVeil',veil,textured({...edged(2.5,.35),hard:true},FX.cloud,[2,2]));
 w.write(path.join(out,'SkaldLute'+copy+'.nif'),w.node('NiNode','SkaldLute',[
  w.node('NiNode','Instrument',[body]),w.node('NiNode','LuteVeil',[shell])]));
}
const {mote}=require('./ParticleTypes.cjs');
require('./NativeLjodhs.cjs').build();
for(const [song,name] of ['Fervor','Echoes','Hollow','Glory'].entries()){
 const w=meshWriter();
 const motes=Array.from({length:constant('kBardMotes')},(_,i)=>mote(w,'BardMote',i,song));
 const waves=Array.from({length:constant('kWaveSlots')*constant('kWaveMotes')},(_,i)=>mote(w,'WaveMote',i,song));
 w.write(path.join(out,'Skald'+name+'.nif'),w.node('NiNode','Skald',[...motes,...waves]));
}
for(let song=0;song<4;song++){
 const w=meshWriter(),motes=Array.from({length:constant('kHitMotes')*constant('kHitBursts')},(_,i)=>mote(w,'HitMote',i,song));
 w.write(path.join(out,'SkaldHit'+song+'.nif'),w.node('NiNode','SkaldHit',motes));
}
// Separate musical beats from gameplay waves. Incidental attacks never add extra waves.
const tracks=[];
const selection=JSON.parse(fs.readFileSync(path.join(__dirname,'music-selection.json'),'utf8'));
for(const entry of selection.tracks){
 const b=fs.readFileSync(path.join(root,'assets/sound/fx/FightWithMe/Skald',entry.file));
 const beatSeconds=60/entry.bpm,round=n=>Math.round(n*1000)/1000;
 const beats=Array.from({length:20},(_,i)=>round(entry.phase+i*beatSeconds));
 const waves=beats.filter((_,i)=>i%2===0);
 tracks.push({...entry,path:'Sound\\fx\\FightWithMe\\Skald\\'+entry.file,sha256:crypto.createHash('sha256').update(b).digest('hex'),
  seconds:round(entry.phase+20*beatSeconds),beatsPerWave:2,beats,waves});
}
fs.writeFileSync(path.join(__dirname,'beat-map.json'),JSON.stringify(tracks,null,2)+'\n');
const floats=a=>a.map(n=>n.toFixed(3)+'f').join(',');
let header='#pragma once\n#include <array>\n#include <span>\nnamespace FWM::SkaldMusic {\n';
tracks.forEach((t,i)=>header+='inline constexpr std::array beats'+i+'{'+floats(t.beats)+'};\n');
tracks.forEach((t,i)=>header+='inline constexpr std::array waves'+i+'{'+floats(t.waves)+'};\n');
header+='struct Track {const char* file; float seconds; float bpm; std::span<const float> beats; std::span<const float> waves;};\ninline constexpr std::array<Track,4> tracks{{\n';
tracks.forEach((t,i)=>header+=' {R"('+t.path+')",'+t.seconds+'f,'+t.bpm.toFixed(1)+'f,beats'+i+',waves'+i+'},\n');
header+='}};\n}\n';fs.writeFileSync(path.join(root,'include/FWM/SkaldMusic.h'),header);
console.log(tracks.map(t=>({track:t.file,bpm:t.bpm,seconds:t.seconds,waves:t.waves.length,spacing:120/t.bpm})));

