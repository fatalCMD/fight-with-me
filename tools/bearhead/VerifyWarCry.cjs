// Check the actual exported dragon against the shared runtime animation, not a guessed head size.
const fs = require('fs'), path = require('path'), assert = require('assert');
const {execFileSync} = require('child_process');
const nifio = require('../wolfhead/nifio');
const repo = path.resolve(__dirname, '../..');
const animation = JSON.parse(execFileSync(path.join(repo, 'build/skyrim/Release/FightWithMeBearLook.exe')));
let minimum = Infinity;
for (const grand of [false,true]) {
const frames = grand ? animation.warGrand : animation.warFrames;
for (const suffix of ['', '2', '3', '4']) {
  const nif = nifio.read(fs.readFileSync(path.join(repo, `assets/meshes/FightWithMe/WarCrySabre${grand?'Red':''}${suffix}.nif`)));
  const parents = new Map();
  nif.blocks.forEach((b, i) => (b.children || []).forEach(k => parents.set(k, i)));
  for (const f of frames) {
    if (f.a < .01) continue;
    for (let i = 0; i < nif.blocks.length; ++i) {
      const shape = nif.blocks[i]; if (shape.type !== 'BSTriShape') continue;
      const parent = nif.blocks[parents.get(i)], name = nif.strings[parent.name];
      if (!['Head', 'Jaw'].includes(name)) continue;
      for (const v of shape.verts) {
        if (v.c[3] < 10) continue;
        let [, y, z] = v.pos;
        if (name === 'Jaw') {
          const c = Math.cos(-f.jaw), s = Math.sin(-f.jaw);
          [y, z] = [c*y-s*z+parent.translate[1], s*y+c*z+parent.translate[2]];
        }
        // Heading/yaw don't change height; these are the runtime's jaw and head pitch transforms.
        const worldZ = f.height + f.s * (Math.sin(f.pitch)*y + Math.cos(f.pitch)*z);
        assert(Number.isFinite(worldZ) && worldZ >= 18, `Sabre penetrates ground at ${f.t}s: ${worldZ}`);
        minimum = Math.min(minimum, worldZ);
      }
    }
  }
}
}
console.log(`PASS: all eight sabre meshes retain at least ${minimum.toFixed(2)} units of ground clearance throughout the visible animation.`);
