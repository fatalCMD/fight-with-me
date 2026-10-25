// The restraint that follows enemies affected by Chaos Down: the main circle's broken inward
// seal, dark swirls and narrowing threads at a small personal scale. No unrelated floating rune.
// This builder does not rebuild or alter the main skeleton/draugr and ground animation.
const path = require('path');
const { piece, ribbon, sprite, random, meshWriter, FX } = require('../fx/meshkit.js');
const m = meshWriter(), nodes = [];
const R = 38;
const polar = (r, a, z = 3) => [r * Math.cos(a), r * Math.sin(a), z];
function part(name, geometry, look) {
  const shape = m.shape(`${name}Veil`, geometry, look);
  nodes.push(m.addBlock({ type: 'NiNode', name: m.str(name), extra: [], controller: -1, flags: 0x8000e,
    translate: [0, 0, 0], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: .0001, collision: -1, children: [shape], effects: [] }));
}

// The exact broken concentric lines and paired inward chevrons used by the casting circle,
// reduced to the enemy's feet. No broad filled disc that could hide their footing.
let mesh = piece();
for (let k = 0; k < 12; k++) {
  const angle = k * Math.PI / 6;
  for (const radius of [205, 236]) {
    const arc = Array.from({ length: 15 }, (_, i) => polar(radius / 250 * R, angle + .07 + i / 14 * .36));
    ribbon(mesh, arc, .75, f => Math.sin(Math.PI * f) * .8, { flat: true });
  }
  ribbon(mesh, [polar(228 / 250 * R, angle - .045), polar(213 / 250 * R, angle), polar(228 / 250 * R, angle + .045)], 1.05, .85, { flat: true });
  ribbon(mesh, [polar(217 / 250 * R, angle - .035), polar(202 / 250 * R, angle), polar(217 / 250 * R, angle + .035)], .7, .65, { flat: true });
}
part('BindSeal', mesh, { scale: 4.0, soft: 4 });

// Open, feathered strands of smoke curl inward from the ankles. Gaps between them keep the
// effect airy and preserve the target's silhouette rather than enclosing it in a solid wall.
mesh = piece();
for (let k = 0; k < 3; k++) {
  const phase = k * Math.PI * 2 / 3;
  const points = Array.from({ length: 28 }, (_, i) => {
    const f = i / 27;
    return polar(37 - 12 * f + 2 * Math.sin(f * 7 + phase), phase + f * 1.95, 3 + 29 * f + 3 * Math.sin(f * Math.PI));
  });
  ribbon(mesh, points, f => 9 + 10 * Math.sin(f * Math.PI), f => .50 * Math.pow(Math.sin(f * Math.PI), 1.4), { v: f => f * 1.5 });
}
part('BindMist', mesh, { scale: 4.8, texture: FX.swirls, soft: 12 });

// These echo the main circle's narrowing threads. UV flow travels along the upward-and-inward
// curves, while the pure timeline overlaps their slow contractions without any snapping reset.
for (let layer = 0; layer < 2; layer++) {
  mesh = piece();
  for (let k = 0; k < 3; k++) {
    const phase = k * Math.PI * 2 / 3 + layer * .8;
    const points = Array.from({ length: 32 }, (_, i) => {
      const f = i / 31;
      return polar(39 - 17 * f + 1.5 * Math.sin(f * 6 + phase), phase + f * 2.0, 4 + f * (layer ? 56 : 70));
    });
    ribbon(mesh, points, f => 3.0 + 7.0 * Math.sin(f * Math.PI), f => .64 * Math.pow(Math.sin(f * Math.PI), 1.5), { v: f => f * 1.8 });
  }
  part(`BindThreads${layer}`, mesh, { scale: 5.0, texture: FX.swirlStrip, soft: 10 });
}

const flecks = [], rand = random(9307);
for (let i = 0; i < 12; i++) {
  const p = piece(), angle = rand() * Math.PI * 2;
  sprite(p, 2.1 + 2.5 * rand(), { alpha: .4 + .3 * rand() });
  const shape = m.shape(`BindMote${i}`, p, { scale: 2.2, texture: FX.glowSpot, soft: 5 });
  const at = polar(22 + 13 * rand(), angle, 8 + 53 * rand());
  const billboard = m.billboard(`BindMoteFacing${i}`, [shape]);
  flecks.push(m.addBlock({ type: 'NiNode', name: m.str(`BindMoteAt${i}`), extra: [], controller: -1, flags: 0x8000e,
    translate: at, rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 1, collision: -1, children: [billboard], effects: [] }));
}
nodes.push(m.addBlock({ type: 'NiNode', name: m.str('BindFlecks'), extra: [], controller: -1, flags: 0x8000e,
  translate: [0, 0, 0], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: .0001, collision: -1, children: flecks, effects: [] }));
const root = m.node('BSFadeNode', 'ChaosBinding', nodes);
for (let copy = 0; copy < 16; copy++) m.write(path.resolve(__dirname, '../../assets/meshes/FightWithMe', `ChaosBinding${copy ? copy + 1 : ''}.nif`), root);
