// Healing Circle's generated ivory-gold seal and feathered, textured rising pulses.
// Source artwork lives in art/; convert to BC3 DDS with texconv before building.
// Runtime parts and geometry sizes match src/HealCircle.cpp and src/Circles.cpp.
// node tools\\healcircle\\BuildHealCircle.js

const fs = require('fs');
const path = require('path');
const nifio = require('../wolfhead/nifio.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const R = 100;          // the mesh's radius: HealCircle::kMeshRadius
const MOTES = 24;       // HealCircle::kMotes
const GLINTS = 32;      // HealCircle::kGlints
const GLOW = 'textures\\FightWithMe\\Glow.dds';
const SEAL = 'textures\\FightWithMe\\HealingSeal.dds';
const PULSE = 'textures\\FightWithMe\\HealingPulse.dds';
// The game's own effect textures: cloud for the aura's shimmer, a soft glow spot for the motes.

const GLOW_SPOT = 'textures\\effects\\FXGlowSpotLinearAlpha.dds';
// Restoration's gold, from the floor up to the aura's line.
const LOOK = {
  floor: { tint: [1.0, 0.7, 0.28], scale: 0.35 },
  ring: { tint: [1.0, 0.76, 0.34], scale: 0.75 },
  runes: { tint: [1.0, 0.96, 0.90], scale: 1.15, texture: SEAL },
  curtain: { tint: [1.0, 0.92, 0.74], scale: 1.05, texture: PULSE, uvScale: [1, 1] },


  motes: { tint: [1.0, 0.8, 0.4], scale: 1.1, texture: GLOW_SPOT },  // soft glow sprites, turned to the camera
  // Wide Circle III's own: unseen until the plugin shows them, and only for a circle that wide.
  inner: { tint: [1.0, 0.96, 0.88], scale: 0.55, texture: SEAL, hidden: true },
  heart: { tint: [1.0, 0.97, 0.9], scale: 0.95, hidden: true },
  glints: { tint: [1.0, 0.95, 0.8], scale: 1.2, texture: GLOW_SPOT, hidden: true }
};

// --- Geometry: plain arrays of vertices (position, normal, uv, colour) and triangles -------

function piece() { return { verts: [], tris: [] }; }
function vertex(p, n, alpha, bright = 1, uv = [0.5, 0.5]) {
  const pack = x => Math.round((Math.max(-1, Math.min(1, x)) + 1) / 2 * 255);
  return { pos: p, btx: 0, uv: [nifio.toHalf(uv[0]), nifio.toHalf(uv[1])], n: n.map(pack), bty: 128, t: [255, 128, 128], btz: 128,
    c: [Math.round(255 * bright), Math.round(255 * bright), Math.round(255 * bright), Math.round(255 * Math.max(0, Math.min(1, alpha)))] };
}
const up = [0, 0, 1];

// A flat band from radius a to b at height z, its alpha given at the inner edge, the middle and the outer edge.
function band(out, a, b, z, alphas, segments = 96) {
  const mid = (a + b) / 2;
  const radii = [a, mid, b];
  const base = out.verts.length;
  for (let s = 0; s <= segments; s++) {
    const t = (s / segments) * Math.PI * 2;
    for (let k = 0; k < 3; k++) out.verts.push(vertex([Math.cos(t) * radii[k], Math.sin(t) * radii[k], z], up, alphas[k]));
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < 2; k++) {
      const i = base + s * 3 + k, j = i + 3;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// The floor glow: a disc, faint in the middle, stronger toward the ring.
function disc(out, radius, z, centre, edge, segments = 64) {
  const base = out.verts.length;
  out.verts.push(vertex([0, 0, z], up, centre));
  const rings = [0.5, 0.85, 1.0];
  const alphas = [centre + (edge - centre) * 0.4, edge, 0.0];
  for (let s = 0; s <= segments; s++) {
    const t = (s / segments) * Math.PI * 2;
    rings.forEach((r, k) => out.verts.push(vertex([Math.cos(t) * radius * r, Math.sin(t) * radius * r, z], up, alphas[k])));
  }
  for (let s = 0; s < segments; s++) {
    const i = base + 1 + s * 3, j = i + 3;
    out.tris.push([base, j, i]);
    for (let k = 0; k < 2; k++) out.tris.push([i + k, j + k, i + k + 1], [i + k + 1, j + k, j + k + 1]);
  }
}

// Smooth textured wisps, bent gently inward as they rise and feathered through their height.
function curtain(out, radius, wave, alphas, segments = 256) {
  const base = out.verts.length, n = alphas.length;
  for (let s = 0; s <= segments; s++) {
    const t = (s / segments) * Math.PI * 2, d = [Math.cos(t), Math.sin(t), 0], h = wave(t);
    for (let k = 0; k < n; k++) {
      const f = k / (n - 1);
      const r = radius - 3.5 * f + 1.5 * Math.sin(t * 5 + f * 3) * Math.sin(Math.PI * f);
      out.verts.push(vertex([d[0] * r, d[1] * r, h * f], d, alphas[k], 1, [s / segments * 3, 1 - f]));
    }
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < n - 1; k++) {
      const i = base + s * n + k, j = i + n;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// The white heart: a disc brightest at its middle, fading out to nothing at its edge.
function heart(out, radius, z, segments = 64) {
  const stops = [[0, 1], [0.2, 0.85], [0.45, 0.55], [0.7, 0.25], [1, 0]];
  const base = out.verts.length;
  for (const [r, a] of stops) {
    for (let s = 0; s <= segments; s++) {
      const t = (s / segments) * Math.PI * 2;
      out.verts.push(vertex([Math.cos(t) * radius * r, Math.sin(t) * radius * r, z], up, a));
    }
  }
  const row = segments + 1;
  for (let k = 0; k < stops.length - 1; k++) {
    for (let s = 0; s < segments; s++) {
      const i = base + k * row + s, j = i + row;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// --- The mesh ------------------------------------------------------------------------------

const strings = [];
const str = s => { let i = strings.indexOf(s); if (i < 0) { i = strings.length; strings.push(s); } return i; };
const blocks = [];
const add = b => blocks.push(b) - 1;
const identity = [1, 0, 0, 0, 1, 0, 0, 0, 1];
const desc = 8n | (4n << 8n) | (5n << 16n) | (6n << 20n) | (7n << 24n) | (0x3bn << 44n);

// Its own shader and alpha blocks for every shape: nothing the game keeps per shape is shared.
function glow(look) {
  const shader = add({
    type: 'BSEffectShaderProperty', name: -1, extra: [], controller: -1,
    flags1: 0x80000000 | 0x40000000 | 0x8 | (look.rim ? 0x40 : 0),  // z-test, soft where it meets the ground, vertex alpha, falloff
    flags2: 0x10 | 0x20,                              // double-sided, vertex colours
    uvOffset: [0, 0], uvScale: look.uvScale || [1, 1], source: look.texture || GLOW, clamp: 3, lighting: 0, envMinLod: 0, unused: 0,
    falloff: look.rim || [1, 1, 1, 1], color: [...look.tint, look.hidden ? 0 : 1], colorScale: look.scale, softDepth: 20, greyscale: ''
  });
  const alpha = add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: 0x100d, threshold: 0 });
  return { shader, alpha };
}
function shape(label, p, look) {
  let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
  for (const v of p.verts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k]); hi[k] = Math.max(hi[k], v.pos[k]); }
  const center = lo.map((x, k) => (x + hi[k]) / 2);
  const radius = Math.max(...p.verts.map(v => Math.hypot(v.pos[0] - center[0], v.pos[1] - center[1], v.pos[2] - center[2])));
  const { shader, alpha } = glow(look);
  return add({ type: 'BSTriShape', name: str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0], rotate: identity, scale: 1, collision: -1,
    center, radius, skin: -1, shader, alpha, desc, verts: p.verts, tris: p.tris, particleSize: 0 });
}
const node = (type, label, translate, children) => ({ type, name: str(label), extra: [], controller: -1, flags: 0x8000e, translate, rotate: identity, scale: 1, collision: -1, children, effects: [] });
// A square facing +Z for a glow sprite, under a node the game keeps square to the camera.
function sprite(size) {
  const out = piece(), h = size / 2, base = 0;
  [[-h, -h, 0, 1], [h, -h, 1, 1], [h, h, 1, 0], [-h, h, 0, 0]].forEach(([x, y, u, v]) => out.verts.push(vertex([x, y, 0], up, 1, 1, [u, v])));
  out.tris.push([base, base + 1, base + 2], [base, base + 2, base + 3]);
  return out;
}
const turned = (label, children) => ({ ...node('NiBillboardNode', label, [0, 0, 0], children), mode: 2 });

// The generated seal sits exactly within the healing boundary. The edge is carried by
// its fine artwork; the floor beneath it adds only a little reflected light.
const floor = piece(); disc(floor, R * 1.02, 0.4, 0.08, 0.22, 192);
const ring = piece(); band(ring, R * 0.98, R * 1.02, 0.8, [0, 0.22, 0], 256);
const glyphs = sprite(202);
for (const v of glyphs.verts) v.pos[2] = 1.0;

// Textured upward pulses, feathered both at the foot and across the entire height.
// The top rows reach zero alpha: no outline caps the effect, even during its lift.
const falloff = Array.from({length:25}, (_,i) => {
  const f=i/24; return Math.min(f/0.07,1) * Math.pow(1-f,2.6);
});
const veil = piece(); curtain(veil, R * 0.965, t => 32 + 4*Math.sin(t*3), falloff);
const inner = piece(); curtain(inner, R * 0.945, t => 27 + 3*Math.sin(t*4+1), falloff.map(x=>x*.66));
const innerCircle = sprite(82);
for(const v of innerCircle.verts) v.pos[2]=1.25;
const core = piece(); heart(core, R * 0.32, 0.6, 192);
const circleChildren = [shape('Floor', floor, LOOK.floor), shape('Ring', ring, LOOK.ring)];
circleChildren.push(add(node('NiNode', 'Heart', [0, 0, 0], [shape('HeartGlow', core, LOOK.heart)])));
circleChildren.push(add(node('NiNode', 'Inner', [0, 0, 0], [shape('InnerCircle', innerCircle, LOOK.inner)])));
circleChildren.push(add(node('NiNode', 'Runes', [0, 0, 0], [shape('RuneGlyphs', glyphs, LOOK.runes)])));
circleChildren.push(add(node('NiNode', 'Aura0', [0, 0, 0], [shape('AuraCurtain0', veil, LOOK.curtain)])));
circleChildren.push(add(node('NiNode', 'Aura1', [0, 0, 0], [shape('AuraCurtain1', inner, LOOK.curtain)])));
// The motes: one node each, posed by the plugin round the edge; they start hidden at the middle.
for (let i = 0; i < MOTES; i++) {
  const n = String(i).padStart(2, '0');
  circleChildren.push(add(node('NiNode', `Mote${n}`, [0, 0, 0], [add(turned(`Mote${n}Turn`, [shape(`Orb${n}`, sprite(11), LOOK.motes)]))])));
}
// Wide Circle III's specks, rising all over it: one node each, like the motes.
for (let i = 0; i < GLINTS; i++) {
  const n = String(i).padStart(2, '0');
  circleChildren.push(add(node('NiNode', `Glint${n}`, [0, 0, 0], [add(turned(`Glint${n}Turn`, [shape(`Speck${n}`, sprite(7), LOOK.glints)]))])));
}
const circle = add(node('NiNode', 'Circle', [0, 0, 0], circleChildren));
const root = add(node('BSFadeNode', 'HealingCircle', [0, 0, 0], [circle]));

const order = [root, ...blocks.map((_, i) => i).filter(i => i !== root)];
const renumber = new Map(order.map((old, now) => [old, now]));
const fix = r => (r >= 0 ? renumber.get(r) : r);
const sorted = order.map(i => {
  const b = { ...blocks[i] };
  if (b.children) b.children = b.children.map(fix);
  if (b.type === 'BSTriShape') { b.shader = fix(b.shader); b.alpha = fix(b.alpha); }
  return b;
});
for (const kind of ['shader', 'alpha']) {
  const used = sorted.filter(b => b.type === 'BSTriShape').map(b => b[kind]);
  if (new Set(used).size !== used.length) throw new Error(`a ${kind} block is shared between shapes`);
}
const nif = { headerLine: 'Gamebryo File Format, Version 20.2.0.7\n', version: 0x14020007, endian: 1, user: 12, bsver: 100,
  author: 'Fight With Me', process: '', exportScript: '', types: [], strings, groups: [], blocks: sorted, roots: [0] };
const bytes = nifio.write(nif);
if (!nifio.write(nifio.read(bytes)).equals(bytes)) throw new Error('the written mesh does not read back the same');
const meshFile = path.join(OUT, 'meshes', 'FightWithMe', 'HealingCircle.nif');
fs.mkdirSync(path.dirname(meshFile), { recursive: true });
fs.writeFileSync(meshFile, bytes);
console.log(`wrote HealingCircle.nif: ${bytes.length} bytes, ${sorted.length} blocks, ${sorted.filter(b => b.type === 'BSTriShape').reduce((t, b) => t + b.tris.length, 0)} triangles`);

// Plain white, 16 by 16, uncompressed BGRA: the glow takes its colour from each shader's tint.
{
  const size = 16;
  const header = Buffer.alloc(128);
  header.write('DDS ', 0, 'latin1');
  header.writeUInt32LE(124, 4);
  header.writeUInt32LE(0x1 | 0x2 | 0x4 | 0x8 | 0x1000, 8);
  header.writeUInt32LE(size, 12);
  header.writeUInt32LE(size, 16);
  header.writeUInt32LE(size * 4, 20);
  header.writeUInt32LE(32, 76);
  header.writeUInt32LE(0x40 | 0x1, 80);
  header.writeUInt32LE(32, 88);
  header.writeUInt32LE(0x00ff0000, 92);
  header.writeUInt32LE(0x0000ff00, 96);
  header.writeUInt32LE(0x000000ff, 100);
  header.writeUInt32LE(0xff000000, 104);
  header.writeUInt32LE(0x1000, 108);
  const file = path.join(OUT, GLOW.replace(/\\/g, '/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.concat([header, Buffer.alloc(size * size * 4, 255)]));
  console.log(`wrote ${GLOW}`);
}
