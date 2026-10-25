// Builds Elemental Burst's three blasts from code: meshes\FightWithMe\ElementalBlastFire.nif,
// ElementalBlastFrost.nif and ElementalBlastShock.nif. Each has the "Orb", the little light that flies
// to the target's feet, and the "Blast", one node per moving part (the names Blast::Parts() lists).
// The game's effect textures (meshkit's FX) supply the fire, mist, sparks and lightning; fractured ice
// uses our detailed ElementalIce texture. Glows and small pieces are sprites the game keeps turned
// to the camera. The plugin poses
// every part and sets its colour and strength each frame (src/Blast.cpp holds the numbers), so every
// shader here starts at no strength: nothing shows before the plugin's first frame.
//
//   node tools\blast\BuildBlast.js
//   node tools\blast\BuildBlast.js --frost-only

const path = require('path');
const { random, piece, vertex, UP, ball, band, disc, wall, dome, ribbon, sprite, meshWriter, FX, soft, edged, textured } =
  require('../fx/meshkit.js');
const { iceSlab } = require('./ice.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const R = 250;  // Blast::kMeshRadius
const ICE = 'textures\\FightWithMe\\ElementalIce.dds';
// A textured, translucent body rather than stacked additive white facets. Blasts selects
// PaintBlended for these shapes so their alpha follows the same birth/shatter envelope.
const iceLook = scale => textured({ scale, normal: true, doubleSided: false, rim: [1, 0, .83, 1], soft: 18 }, ICE);

// How brightly each piece glows (its shader's colour scale). The colour itself comes from the plugin.
// The game's textures add far less light than our solid Glow (a fireball puff gives back about an
// eighth of it), and the game's own spells drive theirs at x1.5 (fire) to x4 (lightning), so the
// pieces drawn with them run hot here; the solid ones stay near 1.
const LOOK = {
  orb: { core: 2.2, halo: 1.0, trail: 0.9 },
  fire: { flash: 1.8, core: 2.2, wave: 1.0, dome: 2.2, ground: 1.6, plume: 2.4, tongue: 2.4, ember: 2.4, burst: 2.4, burstCore: 1.4, flare: 1.5 },
  frost: { flash: 1.6, core: 2.0, wave: 0.95, dome: 1.0, ground: 0.9, crack: 1.5, shard: 1.4, flake: 2.2, spire: 1.38, chip: 1.42 },
  shock: { flash: 1.8, core: 2.4, wave: 1.1, dome: 1.0, ground: 1.2, sigil: 0.75, bolt: 3.2, arc: 2.2, spark: 1.6 }
};

// Blast::Parts(), in the same order.
const names = (prefix, count, digits = 2) => Array.from({ length: count }, (_, i) => prefix + String(i).padStart(digits, '0'));
const PARTS = {
  Fire: ['Flash', 'Wave', 'Dome', 'Ground', ...names('Plume', 7), ...names('Tongue', 10), ...names('Ember', 16), ...names('Burst', 6), ...names('Flare', 20)],
  Frost: ['Flash', 'Wave', 'Dome', 'Ground', ...names('Shard', 18), ...names('Flake', 16), 'Spire', ...names('Chip', 12)],
  Shock: ['Flash', 'Wave', 'Dome', 'Ground', ...names('Bolt', 3, 1), ...names('Arc', 8), ...names('Spark', 16), ...names('Twin', 3, 1)]
};

// A sprite of a game texture, kept square to the camera, under a part's node.
function glowSprite(m, label, size, look, texture, atlas = {}) {
  const quad = piece();
  sprite(quad, size, atlas);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured(look, texture))]);
}

// A strip of texture in its own XZ plane facing +Y, from z a to z b along +Z, a_width wide, its texture
// running top (b) to bottom (a) across one cell of an atlas a_cols wide: the plugin turns it about +Z to
// face the camera (Blast::Facing), so one strip reads from any side. Feathered: clear along both edges,
// and fading out over the share of its length a_fadeA and a_fadeB give at each end, so the texture's
// soft glow never shows the strip's own rectangle, however hot it runs.
function strip(out, a, b, width, { cols = 1, cell = 0, alpha = 1, fadeA = 0, fadeB = 0 } = {}) {
  const h = width / 2, u0 = (cell % cols) / cols, u1 = u0 + 1 / cols, base = out.verts.length;
  const along = [[0, 0], [fadeA, 1], [1 - fadeB, 1], [1, 0]].filter(([f], i, all) => i === 0 || i === all.length - 1 || (f > 0 && f < 1));
  if (!fadeA) along[0][1] = 1;
  if (!fadeB) along[along.length - 1][1] = 1;
  for (const [f, k] of along) {
    const z = a + (b - a) * f, v = 1 - f;
    [[-h, u0, 0], [0, (u0 + u1) / 2, 1], [h, u1, 0]].forEach(([x, u, edge]) => out.verts.push(vertex([x, 0, z], [0, 1, 0], alpha * k * edge, 1, [u, v])));
  }
  for (let r = 0; r + 1 < along.length; r++) {
    const s = base + r * 3, t = s + 3;
    out.tris.push([s, s + 1, t + 1], [s, t + 1, t], [s + 1, s + 2, t + 2], [s + 1, t + 2, t + 1]);
  }
}

// A node lifted a_z off its part's own, for a sprite that should sit up off the ground.
function lifted(m, label, z, children) {
  return m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, z], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 1,
    collision: -1, children, effects: [] });
}

// The flying light: a bright core in a soft halo, both turned to the camera, and a tapering trail
// behind it along -X.
function orb(m) {
  const trail = piece();
  ribbon(trail, Array.from({ length: 12 }, (_, i) => [-i / 11 * 140, 0, 0]), f => 13 * (1 - f) + 1, f => 0.95 * Math.pow(1 - f, 1.4));
  return m.node('NiNode', 'Orb', [glowSprite(m, 'OrbCore', 34, { scale: LOOK.orb.core }, FX.glowSpot), glowSprite(m, 'OrbHalo', 90, { scale: LOOK.orb.halo }, FX.glowSoft),
    m.shape('OrbTrail', trail, { scale: LOOK.orb.trail })]);
}

// What all three share: the flash (a soft glow round a white-hot heart, turned to the camera) and the
// shockwave ring at the reach.
function flash(m, look) {
  return m.node('NiNode', 'Flash', [glowSprite(m, 'FlashGlow', 230, { scale: look.flash }, FX.glowSpot), glowSprite(m, 'FlashCore', 80, { scale: look.core }, FX.glowSoft)]);
}
function wave(m, look, extra = []) {
  const ring = piece();
  band(ring, R * 0.78, R * 1.03, 2, [0, 0.12, 0.35, 1, 0]);
  for (const [a, b, s] of extra) band(ring, a, b, 2.2, [0, s, 0]);
  return m.node('NiNode', 'Wave', [m.shape('WaveRing', ring, { scale: look.wave })]);
}
// Cracks over the ground, out to a_radius, fading at the edge.
function crackedGround(radius) {
  const ground = piece();
  disc(ground, radius, 2.4, r => (r < 1 ? 0.95 * (1 - Math.pow(r, 4)) : 0));
  return ground;
}

function buildFire() {
  const m = meshWriter(), L = LOOK.fire, parts = [];
  parts.push(flash(m, L), wave(m, L, [[R * 0.55, R * 0.6, 0.5]]));
  // A ring of flame bursting outward with the shock: a sheet of rising fire round the middle.
  const ringFire = piece();
  wall(ringFire, 170, 115, [0.95, 0.85, 0.45, 0], 96, 5, true);
  parts.push(m.node('NiNode', 'Dome', [m.shape('FlameRing', ringFire, textured({ scale: L.dome }, FX.fireWave))]));
  // Embers glowing in the scorched ground, strongest in a broken ring round the middle.
  const scorch = piece();
  disc(scorch, 220, 3, (r, t) => clamp01(0.35 + 0.65 * Math.sin(Math.PI * Math.min(r * 1.25, 1))) * (r < 1 ? 1 : 0) * (0.7 + 0.3 * Math.sin(t * 7 + r * 9)));
  parts.push(m.node('NiNode', 'Ground', [m.shape('Scorch', scorch, textured({ scale: L.ground }, FX.embers, [2.5, 2.5]))]));
  // The fireball: puffs of the game's own flame, each a different one, turned to the camera.
  PARTS.Fire.filter(n => n.startsWith('Plume')).forEach((name, k) => {
    parts.push(m.node('NiNode', name, [glowSprite(m, `${name}Flame`, 175, { scale: L.plume }, FX.fireball, { cols: 2, rows: 2, cell: k })]));
  });
  // Tongues of fire up along +Z, curling over toward +X (the way the plugin leans them), widest a
  // quarter of the way up: a sheet of rising flame, its tips at the top.
  PARTS.Fire.filter(n => n.startsWith('Tongue')).forEach((name, k) => {
    const flame = piece(), rand = random(300 + k), length = 150 + 30 * rand(), curl = 0.18 + 0.1 * rand();
    const points = Array.from({ length: 16 }, (_, i) => { const f = i / 15; return [length * curl * f * f + 6 * Math.sin(f * 7 + k), 4 * Math.cos(f * 5 + k), length * f]; });
    ribbon(flame, points, f => 46 * Math.pow(1 - f, 0.6) * (0.55 + 0.45 * Math.min(1, f * 4)) + 4, f => Math.min(1, f * 8), { v: f => 1 - f });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Flame`, flame, textured({ scale: L.tongue }, FX.fireWave))]));
  });
  PARTS.Fire.filter(n => n.startsWith('Ember')).forEach(name => {
    parts.push(m.node('NiNode', name, [glowSprite(m, `${name}Glow`, 20, { scale: L.ember }, FX.spark)]));
  });
  // Grand: the smaller explosions round the fireball, each a puff of flame round a hot heart.
  PARTS.Fire.filter(n => n.startsWith('Burst')).forEach((name, k) => {
    parts.push(m.node('NiNode', name, [glowSprite(m, `${name}Flame`, 140, { scale: L.burst }, FX.fireball, { cols: 2, rows: 2, cell: k + 2 }),
      glowSprite(m, `${name}Core`, 46, { scale: L.burstCore }, FX.glowSpot)]));
  });
  // Grand: sparks flying up off the fireball, long streaks with a bright head, the tail back along -X.
  PARTS.Fire.filter(n => n.startsWith('Flare')).forEach(name => {
    const streak = piece();
    ribbon(streak, Array.from({ length: 6 }, (_, i) => [-52 + i / 5 * 52, 0, 0]), f => 2 + 3 * f, f => f * f);
    parts.push(m.node('NiNode', name, [m.shape(`${name}Streak`, streak, { scale: L.flare }), glowSprite(m, `${name}Head`, 16, { scale: L.flare }, FX.glowSpot)]));
  });
  return finish(m, 'Fire', parts);
}

function buildFrost() {
  const m = meshWriter(), L = LOOK.frost, parts = [];
  parts.push(flash(m, L), wave(m, L, [[R * 0.9, R * 0.93, 0.6]]));
  // A wall of frost mist rolling outward, thick at the ground.
  const mist = piece();
  wall(mist, 230, 90, [0.9, 0.55, 0.18, 0]);
  wall(mist, 180, 60, [0.65, 0.3, 0]);
  parts.push(m.node('NiNode', 'Dome', [m.shape('Mist', mist, textured({ scale: L.dome }, FX.frostCloud))]));
  // Frost over the ground, cracked.
  const patch = piece();
  disc(patch, 240, 2.5, (r, t) => (r < 1 ? clamp01(0.3 + 0.55 * Math.sin(Math.PI * Math.min(r * 1.1, 1)) + 0.15 * Math.sin(t * 11)) * (1 - Math.pow(r, 6)) : 0));
  parts.push(m.node('NiNode', 'Ground', [m.shape('Frost', patch, textured({ scale: L.ground }, FX.frostCloud, [2, 2])),
    m.shape('Cracks', crackedGround(235), textured({ scale: L.crack }, FX.cracks, [1.6, 1.6]))]));
  // Glacier plates break through the ground at unequal angles, with chipped crowns, cloudy
  // fractures and small plates grown against their feet. Each cluster has its own silhouette.
  PARTS.Frost.filter(n => n.startsWith('Shard')).forEach((name, k) => {
    const ice = piece(), r = random(500 + k);
    const heading = r() * Math.PI;
    const height = 99 + 42 * r(), width = 17 + 9 * r();
    iceSlab(ice, [0, 0, -8], height, width, { seed: 5000 + k * 10, depth: .28 + .23 * r(), lean: .035 + .09 * r(), facing: heading, turn: heading });
    iceSlab(ice, [7 + 7 * r(), 7, -7], 52 + 37 * r(), 11 + 6 * r(), { seed: 5001 + k * 10, depth: .3 + .25 * r(), lean: .2 + .12 * r(), facing: .7, turn: heading + .7 });
    iceSlab(ice, [-8 - 6 * r(), -6, -6], 27 + 26 * r(), 9 + 5 * r(), { seed: 5002 + k * 10, depth: .4, lean: .18 + .2 * r(), facing: -1.5, turn: heading - .65 });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Ice`, ice, iceLook(L.shard)), iceAura(m, name, height * 1.14, width * 1.55, 2400 + k, 2)]));
  });
  // Glints of ice drifting up, each a different one, turned to the camera.
  PARTS.Frost.filter(n => n.startsWith('Flake')).forEach((name, k) => {
    parts.push(m.node('NiNode', name, [glowSprite(m, `${name}Ice`, 24, { scale: L.flake }, FX.iceShards, { cols: 4, rows: 4, cell: k })]));
  });
  // Grand: a heaved, broken mass of glacier ice. Broad interlocking plates replace the polished
  // central spear, with large sloping fracture surfaces and smaller pieces around its foot.
  {
    const ice = piece(), r = random(1234);
    iceSlab(ice, [-26, -8, -10], 302, 82, { seed: 12340, depth: .56, lean: .2, facing: 2.4, turn: .35, crownSlope: -.27 });
    iceSlab(ice, [31, -13, -8], 262, 69, { seed: 12341, depth: .59, lean: .27, facing: .5, turn: 1.2, crownSlope: .23 });
    iceSlab(ice, [-8, 29, -9], 223, 60, { seed: 12342, depth: .52, lean: .23, facing: 1.8, turn: 2.5, crownSlope: -.21 });
    [[52, .15, 211, 42, .3], [55, 1.7, 173, 38, .34], [59, 3.3, 181, 42, .36], [47, 4.7, 139, 34, .39], [43, .9, 106, 29, .42]].forEach(([at, t, h, w, lean], k) => {
      iceSlab(ice, [Math.cos(t) * at, Math.sin(t) * at, -8], h, w, { seed: 12350 + k, depth: .33 + .18 * r(), lean, facing: t, turn: t + .3 + .5 * r() });
    });
    for (let k = 0; k < 8; k++) {
      const t = k / 8 * Math.PI * 2 + .4 * r(), at = 57 + 20 * r();
      iceSlab(ice, [Math.cos(t) * at, Math.sin(t) * at, -6], 32 + 45 * r(), 13 + 9 * r(), { seed: 12360 + k, depth: .28 + .24 * r(), lean: .3 + .3 * r(), facing: t, turn: t + r() });
    }
    parts.push(m.node('NiNode', 'Spire', [m.shape('SpireIce', ice, iceLook(L.spire)), iceAura(m, 'Spire', 335, 104, 2470, 3)]));
  }
  // Grand: irregular, closed pieces of the same ice flung out as the mass shatters.
  PARTS.Frost.filter(n => n.startsWith('Chip')).forEach((name, k) => {
    const ice = piece(), r = random(1300 + k);
    const height = 29 + 24 * r();
    iceSlab(ice, [0, 0, -height * .45], height, 8 + 10 * r(), { seed: 13000 + k, depth: .35 + .3 * r(), lean: .08 + .2 * r(), facing: r() * Math.PI * 2, turn: r() * Math.PI });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Ice`, ice, iceLook(L.chip))]));
  });
  return finish(m, 'Frost', parts);
}

// Cold vapor rising beside the ice in a few loose, curling veils. Each cross-ribbon is feathered
// sideways and at both ends; no closed wall or ring can outline the aura. Its own child node lets
// the sampler lift, sway and fade the mist independently of the solid ice. V increases with height,
// so the runtime's negative V drift carries the cloud texture upward.
function iceAura(m, name, height, radius, seed, count) {
  const mist = piece(), rand = random(seed);
  for (let k = 0; k < count; k++) {
    const phase = k / count * Math.PI * 2 + rand() * .8, startV = rand();
    const points = Array.from({ length: 17 }, (_, i) => {
      const f = i / 16, angle = phase + .32 * Math.sin(f * 3.6 + phase);
      const reach = radius * (.68 + .13 * Math.sin(f * 5.2 + phase) + .18 * f * f);
      return [Math.cos(angle) * reach + radius * .12 * Math.sin(f * 3.1), Math.sin(angle) * reach, height * f - 4];
    });
    ribbon(mist, points, f => radius * (.45 + .42 * Math.sin(Math.PI * f)) * (1 - .35 * f),
      f => .27 * Math.pow(Math.sin(Math.PI * f), .9), { v: f => startV + f * 1.3 });
  }
  return m.node('NiNode', `${name}Aura`, [m.shape(`${name}AuraMist`, mist, textured({ scale: 1.7, soft: 38 }, FX.frostCloud))]);
}

function buildShock() {
  const m = meshWriter(), L = LOOK.shock, parts = [];
  parts.push(flash(m, L), wave(m, L, [[R * 0.7, R * 0.73, 0.7]]));
  // Plasma over the spot: a soft electric glow, with swirling light in its heart.
  const plasma = piece(); dome(plasma, 60, { full: true, centre: [0, 0, 55], rings: 12 });
  parts.push(m.node('NiNode', 'Dome', [lifted(m, 'PlasmaLift', 55, [glowSprite(m, 'PlasmaGlow', 300, { scale: L.dome }, FX.glowSoft)]),
    m.shape('Plasma', plasma, textured(edged(L.dome * 0.6, 0.05), FX.swirls, [2, 1]))]));
  // A burnt sigil: two rings over scorched, cracked ground.
  const sigil = piece();
  band(sigil, 132, 142, 2.5, [0, 1, 0]);
  band(sigil, 196, 201, 2.5, [0, 0.8, 0]);
  parts.push(m.node('NiNode', 'Ground', [m.shape('Sigil', sigil, { scale: L.sigil }), m.shape('Scorch', crackedGround(215), textured({ scale: L.ground }, FX.cracks, [1.4, 1.4]))]));
  // Lightning out of the sky: the game's own bolt, top to bottom, one strip turned to the camera.
  PARTS.Shock.filter(n => n.startsWith('Bolt')).forEach((name, k) => {
    const bolt = piece();
    strip(bolt, -20, 1150, 320, { cols: 2, cell: k, fadeB: 0.2 });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Light`, bolt, textured({ scale: L.bolt }, FX.bolt)), glowSprite(m, `${name}Foot`, 150, { scale: L.core }, FX.glowSpot)]));
  });
  // Arcs crawling over the ground out to the reach: a bolt along +Z, which the plugin lays out from the
  // middle and turns about its length to face the camera.
  PARTS.Shock.filter(n => n.startsWith('Arc')).forEach((name, k) => {
    const arc = piece();
    strip(arc, R, 16, 90, { cols: 2, cell: k % 2, fadeA: 0.3, fadeB: 0.08 });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Light`, arc, textured({ scale: L.arc }, FX.bolt))]));
  });
  // Sparks: short streaks, the head at the node and the tail back along -X.
  PARTS.Shock.filter(n => n.startsWith('Spark')).forEach(name => {
    const streak = piece();
    ribbon(streak, Array.from({ length: 5 }, (_, i) => [-34 + i / 4 * 34, 0, 0]), 3.4, f => f);
    parts.push(m.node('NiNode', name, [m.shape(`${name}Streak`, streak, { scale: L.spark })]));
  });
  // Grand: the second thunderbolt, its strip as long as the slant from the sky off to one side down to
  // the spot; the plugin leans it in and turns it to the camera.
  PARTS.Shock.filter(n => n.startsWith('Twin')).forEach((name, k) => {
    const bolt = piece();
    strip(bolt, -20, Math.hypot(430, 1150), 300, { cols: 2, cell: 1 - (k % 2), fadeB: 0.2 });
    parts.push(m.node('NiNode', name, [m.shape(`${name}Light`, bolt, textured({ scale: L.bolt }, FX.bolt))]));
  });
  return finish(m, 'Shock', parts);
}

function clamp01(x) { return Math.max(0, Math.min(1, x)); }

// Written twice, the second as ElementalBlast<Element>2.nif: the game gives no body to a second copy
// of a mesh while one still plays, so a burst right after another takes the other name.
function finish(m, element, parts) {
  const blast = m.node('NiNode', 'Blast', parts);
  const root = m.node('BSFadeNode', `ElementalBlast${element}`, [orb(m), blast]);
  for (const name of [`ElementalBlast${element}.nif`, `ElementalBlast${element}2.nif`]) m.write(path.join(OUT, 'meshes', 'FightWithMe', name), root);
}

const frostOnly = process.argv.includes('--frost-only');
if (!frostOnly) buildFire();
buildFrost();
if (!frostOnly) buildShock();
