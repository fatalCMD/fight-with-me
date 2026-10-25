// Local listening references from the installed game, never distributed with the mod.
const fs=require('fs'),path=require('path'),zlib=require('zlib');
const {execFileSync}=require('child_process');
const {open}=require('../bsa');
function buildAudio(repo,vanilla,out,bastion) {
  const plans=JSON.parse(execFileSync(path.join(repo,'build/skyrim/Release/FightWithMeSoundLook.exe'),{encoding:'utf8'}));
  // These effects already have engine-timed sounds; the preview mirrors their descriptors.
  const existing={breathFire:0x03EF56,breathFrost:0x0DAB87,breathShock:0x03F205,bear:0x0F95BC,blow:0x0E4CAD,slam:0x0C4E30,
    rocks:0x03EA9C,rumble:0x101F41,crumble:0x03C8A2,fire:0x02518E,fireAfter:0x02518F,
    frost:0x023992,frostAfter:0x03EDD8,shock:0x044F5C,shockAfter:0x03F20D,heal:0x03F361,rejuvenate:0x03D118};
  const wanted=new Set(Object.values(existing));
  for(const ranks of Object.values(plans))for(const plan of ranks)for(const cue of plan)wanted.add(cue.form);
  for(const cue of bastion.audio){wanted.add(cue.form);wanted.add(cue.grandForm);}
  const data=fs.readFileSync(path.join(vanilla,'Skyrim.esm')),records=new Map();
  function walk(start,end){for(let at=start;at<end;){const type=data.toString('ascii',at,at+4),size=data.readUInt32LE(at+4);
    if(type==='GRUP'){walk(at+24,at+size);at+=size;continue;}
    const id=data.readUInt32LE(at+12);
    if(type==='SNDR'&&wanted.has(id)){
      let b=data.subarray(at+24,at+24+size);if(data.readUInt32LE(at+8)&0x40000)b=zlib.inflateSync(b.subarray(4));
      const record={id,paths:[]};
      for(let p=0;p+6<=b.length;){const tag=b.toString('ascii',p,p+4),n=b.readUInt16LE(p+4),text=b.toString('utf8',p+6,p+6+n).replace(/\0/g,'');
        if(tag==='EDID')record.name=text;if(tag==='ANAM')record.paths.push(text);p+=6+n;}
      records.set(id,record);
    }at+=24+size;
  }}walk(0,data.length);
  const archive=open(path.join(vanilla,'Skyrim - Sounds.bsa'));
  const files=new Map(archive.files.map(f=>[f.path.replaceAll('\\','/').toLowerCase(),f])),assets={};
  fs.mkdirSync(path.join(out,'audio'),{recursive:true});
  for(const id of wanted){
    const record=records.get(id);if(!record)throw Error(`Missing sound descriptor ${id.toString(16)}`);
    const names=record.paths.map(p=>'sound/'+p.replaceAll('\\','/').replace(/^\/+|\/+$/g,'').replace(/^data\//i,'').replace(/^sound\//i,'').toLowerCase());
    const file=names.map(n=>files.get(n)).find(Boolean);if(!file)throw Error(`No sound file for ${record.name}`);
    const bytes=archive.read(file);if(bytes.toString('ascii',0,4)!=='RIFF')throw Error(`Preview needs WAV conversion: ${record.name}`);
    const relative=`audio/${id.toString(16)}.wav`;fs.writeFileSync(path.join(out,relative),bytes);
    assets[id]={file:relative,name:record.name};
  }
  fs.writeFileSync(path.join(out,'audio-plans.json'),JSON.stringify({plans,existing,assets}));
  console.log(`Audio: ${Object.keys(plans).length} shared sound families, ${wanted.size} verified Skyrim descriptors.`);
}
module.exports={buildAudio};
