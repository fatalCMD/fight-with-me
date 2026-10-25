// Builds meshes\FightWithMe\WaterCloak.nif, Renewal's look: the cloak of water the healer wears, a
// clear sheath shaped to a body that the game's own flowing water runs down, light flowing through it,
// streams spiralling round them from a swirl over their head into a rippling pool at their feet, drops
// falling off it; and eight washes, each a band of the same water that runs down someone healed, head to
// foot, and the ripple it leaves. Everything starts dark: the plugin poses every part and sets its
// colour and strength each frame (src/WaterCloak.cpp holds the numbers).
//
//   node tools\watercloak\BuildWaterCloak.js

const path = require('path');
const { piece, band, disc, ribbon, sprite, meshWriter, FX, textured, edged, turned } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const DROPS = 10;    // WaterCloak::kDrops
const WASHES = 8;    // WaterCloak::kWashes
const WATER = {
  flow: 'textures\\effects\\FXfluidTile01.dds',             // foaming water that tiles, as the game's waterfalls run
  caustic: 'textures\\effects\\MagicCausticTile.dds',       // light through water
  stream: 'textures\\effects\\WaterStream00.dds',           // a stream's glints
  ripples: 'textures\\effects\\FXWaterDropRipplesTile.dds', // drops rippling a pool
  drops: 'textures\\effects\\FluidDrops.dds'                // eight drops, 4 by 2
};

// How brightly each piece glows (its shader's colour scale). The game's water gives back far less
// light than our solid Glow, so the pieces drawn with it run hot.
const LOOK = { veil: 1.6, inner: 1.8, streams: 2.2, crown: 1.6, crownGlow: 0.7, gleam: 1.0, gleamCore: 1.8, pool: 2.0, ripple: 0.8, drop: 1.2, band: 1.8, ring: 0.9 };

const COLUMN = [[4, 40], [18, 40], [48, 40], [84, 40], [120, 40], [150, 40], [168, 40]];

const two = n => String(n).padStart(2, '0');

function glowSprite(m, label, size, look, texture, atlas = {}) {
  const quad = piece();
  sprite(quad, size, atlas);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured(look, texture))]);
}

function at(m, label, z, children) {
  return m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, z], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 1,
    collision: -1, children, effects: [] });
}

function build() {
  const m = meshWriter(), cloak = [], parts = [];

  // The sheath: clear face on, the water showing at its edges, gone at the feet where it meets the pool.
  const veil = piece();
  turned(veil, COLUMN, { scale: 1.12, repeat: 3, vScale: 2.5, alpha: f => Math.min(1, f / 0.08) * (0.55 + 0.45 * Math.min(1, (1 - f) / 0.15)) });
  cloak.push(m.node('NiNode', 'Veil', [m.shape('VeilWater', veil, textured(edged(LOOK.veil, 0.12), WATER.flow, [1, 1]))]));
  // Light flowing down through them, closer in.
  const inner = piece();
  turned(inner, COLUMN, { scale: 0.8, repeat: 2, vScale: 1.6, alpha: f => Math.min(1, f / 0.1) * Math.min(1, (1 - f) / 0.2) * 0.8 });
  cloak.push(m.node('NiNode', 'Inner', [m.shape('InnerLight', inner, textured(edged(LOOK.inner, 0.25), WATER.caustic, [1, 1]))]));
  // Three streams spiralling down round them from the swirl over their head, close to the body.
  const streams = piece();
  for (let k = 0; k < 3; k++) {
    const points = Array.from({ length: 40 }, (_, i) => {
      const f = i / 39, z = 166 - 160 * f, angle = k / 3 * Math.PI * 2 + f * Math.PI * 2 * 1.3;
      const r = 43 + 1.5 * Math.sin(f * Math.PI * 2);
      return [Math.cos(angle) * r, Math.sin(angle) * r, z];
    });
    ribbon(streams, points, f => 6 + 9 * Math.sin(Math.PI * Math.min(f * 1.1, 1)), f => Math.min(1, f / 0.06) * Math.min(1, (1 - f) / 0.08), { vScale: 90 });
  }
  cloak.push(m.node('NiNode', 'Streams', [m.shape('StreamsWater', streams, textured({ scale: LOOK.streams }, WATER.stream, [1, 1]))]));
  // The swirl over their head the water pours from: a whirl of it round a soft glow.
  const whirl = piece();
  band(whirl, 33, 46, 166, [0, 0.8, 1, 0.6, 0], 48);
  cloak.push(m.node('NiNode', 'Crown', [m.shape('CrownWater', whirl, textured({ scale: LOOK.crown }, WATER.flow, [1, 1])), at(m, 'CrownLift', 168, [glowSprite(m, 'CrownGlow', 44, { scale: LOOK.crownGlow }, FX.glowSoft)])]));
  // The third rank's glow over the head: a wide soft light round a bright heart, shown only for it.
  cloak.push(m.node('NiNode', 'Gleam', [at(m, 'GleamLift', 172, [glowSprite(m, 'GleamHalo', 130, { scale: LOOK.gleam }, FX.glowSoft), glowSprite(m, 'GleamCore', 52, { scale: LOOK.gleamCore }, FX.glowSpot)])]));
  // The pool at their feet, drops rippling it, and the rings spreading out over it.
  const pool = piece();
  disc(pool, 75, 1.2, r => (r < 1 ? 0.85 * (1 - Math.pow(r, 3)) : 0), 48, 8);
  cloak.push(m.node('NiNode', 'Pool', [m.shape('PoolWater', pool, textured({ scale: LOOK.pool }, WATER.ripples, [2, 2]))]));
  for (let k = 0; k < 3; k++) {
    const ring = piece();
    band(ring, 25, 31, 1.6, [0, 1, 0], 48);
    cloak.push(m.node('NiNode', `Ripple${k}`, [m.shape(`Ripple${k}Ring`, ring, { scale: LOOK.ripple })]));
  }
  for (let k = 0; k < DROPS; k++) {
    cloak.push(m.node('NiNode', `Drop${two(k)}`, [glowSprite(m, `Drop${two(k)}Bead`, 11, { scale: LOOK.drop }, WATER.drops, { cols: 4, rows: 2, cell: k % 8 })]));
  }
  parts.push(m.node('NiNode', 'Cloak', cloak));

  // The washes: a band of the same water round someone, sliding down them, and the ripple it leaves.
  for (let k = 0; k < WASHES; k++) {
    const sheet = piece();
    turned(sheet, [[-24, 33], [-10, 34], [4, 34], [14, 33], [24, 31]], { repeat: 3, vScale: 0.7, alpha: f => Math.pow(Math.sin(Math.PI * f), 1.5) });
    const ring = piece();
    band(ring, 24, 31, 1.6, [0, 1, 0], 48);
    parts.push(m.node('NiNode', `Wash${two(k)}`, [
      m.node('NiNode', `Wash${two(k)}Band`, [m.shape(`Wash${two(k)}Sheet`, sheet, textured(edged(LOOK.band, 0.2), WATER.flow, [1, 1]))]),
      m.node('NiNode', `Wash${two(k)}Ring`, [m.shape(`Wash${two(k)}Ripple`, ring, { scale: LOOK.ring })])
    ]));
  }

  // Written twice, the second as WaterCloak2.nif: the game gives no body to a second copy of a mesh
  // while one still plays, so a second healer's cloak takes the other name.
  const root = m.node('BSFadeNode', 'WaterCloak', parts);
  for (const name of ['WaterCloak.nif', 'WaterCloak2.nif']) m.write(path.join(OUT, 'meshes', 'FightWithMe', name), root);
}

build();
