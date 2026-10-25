// Source is Bethesda's stock Skyrim archive, never the modded virtual filesystem.
const fs = require('fs'), path = require('path');
const {open} = require('../wolfhead/bsa');
const data = process.argv[2] || 'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
let source;
for (const name of ['Skyrim - Meshes0.bsa', 'Skyrim - Meshes1.bsa']) {
 const archive = open(path.join(data,name));
 const file = archive.files.find(f => f.path.toLowerCase() === 'meshes\\magic\\fireballexp01.nif');
 if (file) { source=archive.read(file);break; }
}
if (!source || !source.includes(Buffer.from('SpecialIdle_AreaEffect'))) throw Error('Stock animated fireball asset not found');
const output=path.resolve(__dirname,'../../assets/meshes/FightWithMe/ArrowExplosion.nif');
fs.writeFileSync(output,source);
console.log(`Extracted stock animated explosion: ${source.length} bytes`);
