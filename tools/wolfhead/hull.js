// The lower outline of the visible head, for WolfBite.h's kSkullHull and kJawHull: the skull's points (from
// the head's middle) and the jaw's (from its hinge) that reach furthest down and forward, in every
// direction between straight ahead and straight down. Whatever the jaw's opening and the head's
// tilt, the lowest point of the head is one of these, to within the fur.
//   node tools\wolfhead\hull.js
const fs = require('fs');
const path = require('path');
const nifio = require('./nifio.js');

const nif = nifio.read(fs.readFileSync(path.join(__dirname, '..', '..', 'assets', 'meshes', 'FightWithMe', 'SpectralWolfHead.nif')));
const name = i => (i >= 0 ? nif.strings[i] : '');
const parentOf = new Map();
nif.blocks.forEach((b, i) => { if (b.children) for (const c of b.children) parentOf.set(c, i); });
const points = { Head: [], Jaw: [] };
nif.blocks.forEach((b, i) => {
  // The skin only (the fur adds a few units, allowed for in WolfBite.h), and only where it shows.
  if (b.type === 'BSTriShape' && /Skin$/.test(name(b.name))) points[name(nif.blocks[parentOf.get(i)].name)].push(...b.verts.filter(v => v.c[3] > 100).map(v => v.pos));
});
function hull(list) {
  const out = [];
  for (let k = 0; k <= 8; k++) {
    const a = Math.PI * (-0.25 + 0.75 * k / 8);  // from a little above ahead, round to behind-down
    const d = [0, Math.cos(a), -Math.sin(a)];
    let best = null, most = -1e9;
    for (const p of list) { const s = p[1] * d[1] + p[2] * d[2]; if (s > most) { most = s; best = p; } }
    if (!out.some(q => q[1] === best[1] && q[2] === best[2])) out.push(best);
  }
  return out.map(p => `{ 0.0f, ${p[1].toFixed(1)}f, ${p[2].toFixed(1)}f }`).join(', ');
}
console.log(`kSkullHull: ${hull(points.Head)}`);
console.log(`kJawHull:   ${hull(points.Jaw)}`);
