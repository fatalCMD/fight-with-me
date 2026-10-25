// Original geometry: a short release, broken binding seal and inward-flowing threads.
// Animation lives in src/ChaosPulse.cpp, shared with the browser preview.
const path = require('path');
const { piece, vertex, band, disc, wall, ribbon, crystal, random, meshWriter, FX } = require('../fx/meshkit.js');
const specter = require('./SpecterMesh.js');
const polar = (r, a, z = 4) => [r * Math.cos(a), r * Math.sin(a), z];
function risingMist(phase) {
  const p=piece(), segments=192, rows=24;
  // A curved, feathered veil with rolling density: no straight conical wall or
  // hard top edge. Many vertical samples keep the inward curl smooth as it rises.
  for(let s=0;s<=segments;++s)for(let k=0;k<=rows;++k) {
    const a=s/segments*Math.PI*2, f=k/rows, envelope=Math.sin(Math.PI*f);
    const bend=f*f*(3-2*f), wave=Math.sin(a*5+f*7+phase);
    const r=244-91*bend+3*wave*envelope;
    const z=f*82+9*envelope*Math.sin(a*3+f*4+phase);
    const cloud=Math.pow(Math.max(0,0.56+0.26*wave+0.18*Math.sin(a*11-f*9-phase)),1.5);
    const alpha=0.52*Math.pow(envelope,1.7)*cloud;
    p.verts.push(vertex(polar(r,a,z),[Math.cos(a),Math.sin(a),0.8],alpha));
  }
  for(let s=0;s<segments;++s)for(let k=0;k<rows;++k) {
    const a=s*(rows+1)+k,b=a+rows+1;
    p.tris.push([a,b,a+1],[a+1,b,b+1]);
  }
  return p;
}
function build(grand) {
const m = meshWriter(), nodes = [];
function part(name, geometry, look = { scale: 2.2, soft: 5 }) {
  nodes.push(m.node('NiNode', name, [m.shape(`${name}Glow`, geometry, look)]));
}
let p = piece(); band(p, 226, 250, 3, [0, 0.3, 1, 0.3, 0]); part('Out', p);
p = piece(); band(p, 229, 250, 5, [0, 0.45, 1, 0.25, 0]); part('In', p);
p = piece();
for (let k = 0; k < 12; ++k) {
  const a = k * Math.PI / 6;
  // Broken concentric lines and an inward-pointing angular rune between them.
  for (const r of [205, 236]) {
    const arc = Array.from({ length: 15 }, (_, i) => polar(r, a + 0.07 + i / 14 * 0.36));
    ribbon(p, arc, 3.5, f => Math.sin(Math.PI * f) * 0.9, { flat: true });
  }
  ribbon(p, [polar(228, a - 0.045), polar(213, a), polar(228, a + 0.045)], 5, 1, { flat: true });
  ribbon(p, [polar(217, a - 0.035), polar(202, a), polar(217, a + 0.035)], 3, 0.8, { flat: true });
}
part('Seal', p, { scale: 2.5, soft: 4 });
p = piece(); wall(p, 229, 60, [0, 0.65, 0.35, 0], 96, 6); part('Mist', p, { scale: 1.5, texture: FX.swirls, soft: 15 });
p = piece();
for (let k = 0; k < 8; ++k) {
  const points = Array.from({ length: 36 }, (_, i) => {
    const f = i / 35, a = k * Math.PI / 4 + f * 0.9;
    return polar(228 - f * 150, a, 7 + Math.sin(Math.PI * f) * (18 + k % 3 * 8));
  });
  ribbon(p, points, f => 4 + 8 * Math.sin(Math.PI * f), f => Math.sin(Math.PI * f) * 0.8, { vScale: 100 });
}
part('Threads', p, { scale: 2.0, texture: FX.swirlStrip, soft: 8 });
p = piece(); disc(p, 115, 2, r => Math.pow(1 - r, 2), 72, 12); part('Core', p, { scale: 2.0, soft: 4 });
p = piece(); const rand = random(8149);
for (let i = 0; i < 28; ++i) {
  const a = rand() * Math.PI * 2;
  crystal(p, polar(55 + rand() * 180, a, 6 + rand() * 40), 5 + rand() * 9, 1.0 + rand() * 1.6, 2, a, { alphas: [0, 0.8, 0] });
}
part('Flecks', p, { scale: 3.0, soft: 3 });
for (let i=0;i<3;++i) {
  part(`Rise${i}`,risingMist(i*2.1),{scale:1.6,soft:24});
}
nodes.push(specter(m, grand));
const root = m.node('BSFadeNode', 'ChaosDown', nodes);
for (const copy of ['', '2', '3', '4']) m.write(path.resolve(__dirname, '../../assets/meshes/FightWithMe', `ChaosDown${grand?'Grand':''}${copy}.nif`), root);
}
build(false);
build(true);
