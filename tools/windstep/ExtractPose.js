// Extract only build inputs from the user's stock archive; never ship raw HKX files.
const fs=require('fs'),path=require('path'),{open}=require('../wolfhead/bsa');
const data=process.env.FWM_SKYRIM_DATA||'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const out=path.resolve(__dirname,'../../build/windstep-stock');fs.mkdirSync(out,{recursive:true});
const archive=open(path.join(data,'Skyrim - Animations.bsa'));
for(const [file,name] of [
 ['actors/character/character assets/skeleton.hkx','skeleton.hkx'],
 ['actors/character/character assets female/skeleton_female.hkx','skeleton_female.hkx'],
 ['actors/character/animations/bow_idledrawn.hkx','bow_idledrawn.hkx'],
 ['weapons/bow/character assets/skeleton.hkx','bow-skeleton.hkx'],
 ['weapons/bow/animations/bow_idledrawn.hkx','bow-weapon-idledrawn.hkx'],
]){
 const entry=archive.files.find(f=>f.path.replaceAll('\\','/').toLowerCase()==='meshes/'+file);
 if(!entry)throw Error('Missing Skyrim animation '+file);
 fs.writeFileSync(path.join(out,name),archive.read(entry));
}
console.log('Extracted stock bow pose inputs into '+out);
