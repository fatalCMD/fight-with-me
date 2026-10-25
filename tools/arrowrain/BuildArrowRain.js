// Builds meshes\FightWithMe\ArrowRain.nif, Arrow Rain's look: the hunter's mark laid on the ground
// under the rain, the glow in the sky the arrows fall out of, the flare that calls it and its burst,
// and for each of the game's arrows a spectral arrow laid over it, a streak behind it and a hit where it lands. Strokes are our solid
// Glow shaped by their corners' alpha; the sky's swirl, the glows and the dust are the game's own
// textures. The plugin poses every part and sets its colour and strength each frame (src/ArrowRain.cpp
// holds the numbers), so nothing shows before its first frame.
//
//   node tools\arrowrain\BuildArrowRain.js

const path = require('path');
const { piece, vertex, UP, band, disc, ribbon, sprite, meshWriter, FX, textured } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const R = 300;         // ArrowRain::kRadius
const TRAIL = 300;     // ArrowRain::kTrail
const TIP = 61;        // ArrowRain::kTip: the game's arrows reach this far ahead of their nock
const ARROWS = 28;     // ArrowRain::kMaxArrows

// How brightly each piece glows (its shader's colour scale). The solid strokes stay near 1; the game's
// textures give back far less light, so the pieces drawn with them run hot.
const LOOK = {
  ring: 1.1, dashes: 0.9, heads: 1.2, outer: 0.9, floor: 0.45,
  halo: 0.5, swirl: 0.9, crown: 0.7,
  burst: 2.2, burstCore: 2.4, flare: 2.4, flareTrail: 1.1,
  arrow: 1.15, trail: 0.8, glint: 1.0, spark: 2.4, ripple: 1.0, dust: 1.0
};

const two = n => String(n).padStart(2, '0');

// A line a to b over the ground at a_z, a_width wide, bright down its middle and clear at its edges.
function stroke(out, a, b, width, z = 2, alpha = 1) {
  const d = [b[0] - a[0], b[1] - a[1]], l = Math.hypot(...d) || 1, side = [-d[1] / l * width / 2, d[0] / l * width / 2];
  const base = out.verts.length;
  for (const p of [a, b]) {
    for (const [k, edge] of [[-1, 0], [0, 1], [1, 0]]) out.verts.push(vertex([p[0] + side[0] * k, p[1] + side[1] * k, z], UP, alpha * edge));
  }
  out.tris.push([base, base + 1, base + 4], [base, base + 4, base + 3], [base + 1, base + 2, base + 5], [base + 1, base + 5, base + 4]);
}

// Part of a ring, a to b out from the middle over the angles t0..t1, its alpha across from a to b.
function arc(out, a, b, z, t0, t1, alphas, segments = 6) {
  const base = out.verts.length, n = alphas.length;
  for (let s = 0; s <= segments; s++) {
    const t = t0 + (t1 - t0) * s / segments, fade = Math.min(1, Math.min(s, segments - s) / 1.5);
    for (let k = 0; k < n; k++) {
      const r = a + (b - a) * k / (n - 1);
      out.verts.push(vertex([Math.cos(t) * r, Math.sin(t) * r, z], UP, alphas[k] * fade));
    }
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < n - 1; k++) {
      const i = base + s * n + k, j = i + n;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// An arrowhead at a_r out at the angle a_t, pointing in to the middle: two strokes meeting at its tip.
function chevron(out, r, t, size, width) {
  const dir = [Math.cos(t), Math.sin(t)], side = [-dir[1], dir[0]];
  const tip = [dir[0] * (r - size / 2), dir[1] * (r - size / 2)];
  for (const k of [-1, 1]) {
    const end = [tip[0] + dir[0] * size + side[0] * size * 0.62 * k, tip[1] + dir[1] * size + side[1] * size * 0.62 * k];
    stroke(out, tip, end, width);
  }
}

// A shape in the XZ plane facing +Y, as rows up +Z, each row [z, half-width, centre alpha, edge alpha]:
// three corners across, bright down the middle, softer at its edges.
function rows(out, list, x0 = 0) {
  const base = out.verts.length;
  for (const [z, h, k, edge] of list) {
    for (const [x, a] of [[x0 - h, edge], [x0, k], [x0 + h, edge]]) out.verts.push(vertex([x, 0, z], [0, 1, 0], a));
  }
  for (let r = 0; r + 1 < list.length; r++) {
    const s = base + r * 3, t = s + 3;
    out.tris.push([s, s + 1, t + 1], [s, t + 1, t], [s + 1, s + 2, t + 2], [s + 1, t + 2, t + 1]);
  }
}

// A streak up along +Z from just behind the nock, a_width wide: clear along its edges, fading out up
// its length. The plugin turns it about +Z to face the camera.
function trail(out, length, width) {
  const h = width / 2;
  rows(out, [[8, h * 0.5, 0, 0], [16, h, 1, 0], [0.2 * length, h, 0.7, 0], [0.5 * length, h, 0.3, 0], [length, h * 0.6, 0, 0]]);
}

// A spectral arrow along +Z, a little longer and broader than the game's so it reads from afar: its
// broadhead down at -TIP, its shaft, and its fletching about the nock at the origin, which is where the
// game puts a flying arrow.
function spectralArrow(out, w = 1.4) {
  const tip = -TIP - 8, neck = -TIP + 13;
  rows(out, [[tip, 0, 1, 1], [tip + 4, 3 * w, 1, 0.55], [tip + 11, 7.5 * w, 1, 0.35], [neck, 2.5 * w, 1, 0.6]]);  // the broadhead
  rows(out, [[neck - 1, 2.2 * w, 1, 0], [-6, 2.2 * w, 0.95, 0], [9, 2.2 * w, 0.85, 0]]);                          // the shaft
  for (const side of [-1, 1]) {
    // A vane each side, swept back from the shaft.
    const base = out.verts.length;
    const points = [[0, -10, 0.9], [0, 9, 0.85], [8 * w * side, 12, 0.3], [8 * w * side, -2, 0.3]];
    for (const [x, z, a] of points) out.verts.push(vertex([x, 0, z], [0, 1, 0], a));
    out.tris.push([base, base + 1, base + 2], [base, base + 2, base + 3]);
  }
}

// A sprite of a game texture, kept square to the camera.
function glowSprite(m, label, size, look, texture, atlas = {}) {
  const quad = piece();
  sprite(quad, size, atlas);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured(look, texture))]);
}

function build() {
  const m = meshWriter(), parts = [];

  // The hunter's mark: a ring with ticks round it turning slowly, a dashed ring turning the other way
  // round a small one at the middle, arrowheads round the rim pointing in, and a glow over the ground,
  // brightest toward the rim.
  const ring = piece();
  band(ring, R * 0.965, R * 1.0, 2.2, [0, 1, 0.85, 0]);
  band(ring, R * 0.9, R * 1.06, 2.0, [0, 0.22, 0]);
  for (let k = 0; k < 16; k++) {
    const t = k / 16 * Math.PI * 2, long = k % 4 === 0;
    stroke(ring, [Math.cos(t) * R * 1.01, Math.sin(t) * R * 1.01], [Math.cos(t) * R * (long ? 1.11 : 1.06), Math.sin(t) * R * (long ? 1.11 : 1.06)], long ? 9 : 6, 2.2);
  }
  const dashes = piece();
  for (let k = 0; k < 24; k++) {
    const t0 = k / 24 * Math.PI * 2;
    arc(dashes, R * 0.84, R * 0.87, 2.4, t0, t0 + Math.PI * 2 / 24 * 0.6, [0, 1, 0]);
  }
  band(dashes, R * 0.1, R * 0.125, 2.4, [0, 1, 0], 48);
  for (let k = 0; k < 4; k++) {
    const t = k / 4 * Math.PI * 2 + Math.PI / 4;
    stroke(dashes, [Math.cos(t) * R * 0.14, Math.sin(t) * R * 0.14], [Math.cos(t) * R * 0.22, Math.sin(t) * R * 0.22], 6, 2.4);
  }
  const heads = piece();
  for (let k = 0; k < 8; k++) chevron(heads, R * 0.93, k / 8 * Math.PI * 2 + Math.PI / 8, 46, 10);
  const outer = piece();
  band(outer, R * 1.14, R * 1.17, 2.0, [0, 1, 0]);
  for (let k = 0; k < 8; k++) {
    const t = k / 8 * Math.PI * 2;
    chevron(outer, R * 1.25, t, 30, 7);
  }
  const floor = piece();
  disc(floor, R, 1.6, r => (r < 1 ? (0.12 + 0.88 * Math.pow(r, 3)) * (1 - Math.pow(r, 12)) : 0), 72, 12);
  parts.push(m.node('NiNode', 'Mark', [
    m.node('NiNode', 'Ring', [m.shape('MarkRing', ring, { scale: LOOK.ring })]),
    m.node('NiNode', 'Dashes', [m.shape('MarkDashes', dashes, { scale: LOOK.dashes })]),
    m.node('NiNode', 'Heads', [m.shape('MarkHeads', heads, { scale: LOOK.heads })]),
    m.node('NiNode', 'Outer', [m.shape('MarkOuter', outer, { scale: LOOK.outer })]),
    m.node('NiNode', 'Floor', [m.shape('MarkFloor', floor, { scale: LOOK.floor })])
  ]));

  // The sky the arrows fall out of: a soft glow facing down, a swirl of cloud turning in it, and the
  // grand sky's crown of light round it.
  const halo = piece();
  disc(halo, 360, 0, r => 0.85 * Math.pow(Math.max(0, 1 - r), 1.5), 64, 10);
  const swirl = piece();
  disc(swirl, 330, -4, r => (r < 1 ? 0.15 + 0.75 * Math.sin(Math.PI * Math.min(r * 1.15, 1)) : 0), 64, 10);
  const crown = piece();
  band(crown, R * 1.18, R * 1.23, -6, [0, 1, 0]);
  for (let k = 0; k < 12; k++) {
    const t = k / 12 * Math.PI * 2;
    stroke(crown, [Math.cos(t) * R * 1.25, Math.sin(t) * R * 1.25], [Math.cos(t) * R * 1.4, Math.sin(t) * R * 1.4], 10, -6);
  }
  parts.push(m.node('NiNode', 'Sky', [
    m.node('NiNode', 'Halo', [m.shape('SkyHalo', halo, { scale: LOOK.halo })]),
    m.node('NiNode', 'Swirl', [m.shape('SkySwirl', swirl, textured({ scale: LOOK.swirl }, FX.cloud, [1.6, 1.6]))]),
    m.node('NiNode', 'Crown', [m.shape('SkyCrown', crown, { scale: LOOK.crown })])
  ]));

  // The flare bursting up there, and the flare itself: a bright head and its trail back along -X.
  parts.push(m.node('NiNode', 'Burst', [glowSprite(m, 'BurstGlow', 260, { scale: LOOK.burst }, FX.glowSpot), glowSprite(m, 'BurstCore', 110, { scale: LOOK.burstCore }, FX.glowSoft)]));
  const flareTrail = piece();
  ribbon(flareTrail, Array.from({ length: 14 }, (_, i) => [-i / 13 * 220, 0, 0]), f => 9 * (1 - f) + 1, f => 0.95 * Math.pow(1 - f, 1.3));
  parts.push(m.node('NiNode', 'Flare', [glowSprite(m, 'FlareHead', 46, { scale: LOOK.flare }, FX.glowSpot), m.shape('FlareTrail', flareTrail, { scale: LOOK.flareTrail })]));

  // Every arrow: the spectral arrow over the game's, its streak, the glint at its point; and the hit
  // where it lands: a spark, a ripple over the ground, a puff of the game's own dust (laid over the
  // scene, not glowing), each puff a different one.
  const glintAt = (label, children) => m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, -TIP - 6],
    rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 1, collision: -1, children, effects: [] });
  for (let k = 0; k < ARROWS; k++) {
    const shape = piece(), line = piece();
    spectralArrow(shape);
    trail(line, TRAIL, 7);
    parts.push(m.node('NiNode', `Arrow${two(k)}`, [
      m.node('NiNode', `Arrow${two(k)}Shape`, [m.shape(`Arrow${two(k)}Body`, shape, { scale: LOOK.arrow })]),
      m.node('NiNode', `Arrow${two(k)}Trail`, [m.shape(`Arrow${two(k)}Line`, line, { scale: LOOK.trail })]),
      glintAt(`Arrow${two(k)}Glint`, [glowSprite(m, `Arrow${two(k)}Point`, 12, { scale: LOOK.glint }, FX.glowSpot)])
    ]));
  }
  for (let k = 0; k < ARROWS; k++) {
    const ripple = piece();
    band(ripple, 22, 44, 1.5, [0, 1, 0.5, 0], 40);
    parts.push(m.node('NiNode', `Hit${two(k)}`, [
      m.node('NiNode', `Hit${two(k)}Spark`, [glowSprite(m, `Hit${two(k)}Flash`, 70, { scale: LOOK.spark }, FX.glowSpot)]),
      m.node('NiNode', `Hit${two(k)}Ripple`, [m.shape(`Hit${two(k)}Ring`, ripple, { scale: LOOK.ripple })]),
      m.node('NiNode', `Hit${two(k)}Dust`, [glowSprite(m, `Hit${two(k)}Puff`, 110, { scale: LOOK.dust, normal: true }, FX.smoke, { cols: 4, rows: 4, cell: (k * 5) % 16 })])
    ]));
  }

  // Written twice, the second as ArrowRain2.nif: the game gives no body to a second copy of a mesh
  // while one still plays, so a second archer's rain takes the other name.
  const root = m.node('BSFadeNode', 'ArrowRain', parts);
  for (const name of ['ArrowRain.nif', 'ArrowRain2.nif']) m.write(path.join(OUT, 'meshes', 'FightWithMe', name), root);
}

build();
