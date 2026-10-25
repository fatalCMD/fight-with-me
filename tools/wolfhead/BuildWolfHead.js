// Builds the giant head Kynareth's Maw bites with, from Skyrim's own wolf
// (actors\canine\character assets wolf\wolf.nif): its head cut at the neck and frozen in its rest
// pose, as two rigid parts the plugin moves itself, the skull and the lower jaw on a node at the
// jaw's hinge. It wears the wolf's own lit skin, so it looks like the wolves of the game it is in.
//
//   meshes\FightWithMe\SpectralWolfHead.nif       with fur: sixteen shells pushed out from the skin,
//                                                 each cut a little finer by an alpha test, drawn with
//                                                 Fight With Me's generated SpectralFur.dds (the
//                                                 plugin uses this one only when that texture is installed)
//   meshes\FightWithMe\SpectralWolfHeadPlain.nif  without: the vanilla wolf's own fur cards instead
//   meshes\FightWithMe\SpectralWolfHeadRed.nif    the third rank's, red, its fur longer and thicker (more
//   meshes\FightWithMe\SpectralWolfHeadRedPlain.nif shells, further out), and its plain fellow
//
// The shells are Fight With Me's own geometry; no external fur mesh or texture is read or copied.
//
//   node tools\wolfhead\BuildWolfHead.js ["<Skyrim Data folder>"]
//
// Prints the numbers the plugin's bite needs (Arts.cpp kHead*), in the mesh's own units.

const fs = require('fs');
const path = require('path');
const { open } = require('./bsa.js');
const nifio = require('./nifio.js');

const data = process.argv.slice(2).find(a => !a.startsWith('--')) || 'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const outDir = path.join(__dirname, '..', '..', 'assets', 'meshes', 'FightWithMe');

const LOOK = process.argv.includes('--natural') ? 'natural' : 'spectral';
// The spectral looks: the fur's brightness read through a gradient of Fight With Me's own, written here
// (the first ranks' dark blue to pale cyan to white, the third's deep red to ember to near white), and
// how much fur: shells, each pushed this much further out, in the wolf's own units.
const KINDS = {
  blue: { gradient: 'textures\\FightWithMe\\SpectralWolfGradient.dds', stops: [[0, [6, 14, 30]], [0.3, [45, 90, 155]], [0.55, [115, 170, 235]], [0.8, [190, 225, 255]], [1, [242, 250, 255]]],
    shells: 16, step: 0.19, skinTint: [0.9, 0.97, 1.0], furTint: [0.85, 0.95, 1.0], skin: 0.75 },
  red: { gradient: 'textures\\FightWithMe\\SpectralWolfGradientRed.dds', stops: [[0, [26, 2, 2]], [0.3, [118, 8, 6]], [0.55, [198, 24, 14]], [0.8, [238, 64, 34]], [1, [255, 146, 104]]],
    shells: 24, step: 0.25, skinTint: [1.0, 0.85, 0.8], furTint: [1.0, 0.8, 0.75], skin: 0.85 }
};
// How bright each part glows: every fur shell adds its light over the last, so they stay faint.
const SPECTRAL = { rim: [1.0, 0.0, 0.25, 1.0], fur: 0.16, furTips: 0.6 };  // furTips: share lost by the outermost shell
const SHELL_TEXTURE = 'textures\\FightWithMe\\SpectralFur.dds';
const MUZZLE = [60, 71];  // fur shortens from the cheeks (y 60) to the nose (y 71), to a third

const meshes = open(path.join(data, 'Skyrim - Meshes0.bsa'));
const source = meshes.files.find(f => f.path.toLowerCase() === 'meshes\\actors\\canine\\character assets wolf\\wolf.nif');
if (!source) throw new Error('wolf.nif not found in Skyrim - Meshes0.bsa');
const wolf = nifio.read(meshes.read(source));
const name = i => (i >= 0 ? wolf.strings[i] : '');

// Bones at rest, in the mesh's space (the skinned vertices are stored in the same space).
const parent = new Map();
wolf.blocks.forEach((b, i) => { if (b.children) for (const c of b.children) if (c >= 0) parent.set(c, i); });
const mul = (a, b) => { const r = []; for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) { let s = 0; for (let k = 0; k < 3; k++) s += a[i * 3 + k] * b[k * 3 + j]; r.push(s); } return r; };
const apply = (m, v) => [0, 1, 2].map(i => m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2]);
function rest(i) {
  const b = wolf.blocks[i];
  if (!parent.has(i)) return { r: b.rotate, t: b.translate, s: b.scale };
  const p = rest(parent.get(i));
  return { r: mul(p.r, b.rotate), t: apply(p.r, b.translate).map((x, k) => x * p.s + p.t[k]), s: p.s * b.scale };
}
const bone = n => { const i = wolf.blocks.findIndex(b => b.type === 'NiNode' && name(b.name) === n); if (i < 0) throw new Error(`no bone ${n}`); return rest(i).t; };
const hinge = bone('Canine_JawBone');

const HEAD = /Head|Jaw|Tongue|Ear|Eye|Lip/;
const JAW = /Jaw|Tongue/;

// A skinned shape's vertices with how much of each rides on the head and on the jaw, its triangles,
// and the shader and alpha settings it is drawn with.
function pieces(shapeIndex) {
  const shape = wolf.blocks[shapeIndex];
  const skin = new nifio.Reader(wolf.blocks[shape.skin].raw);
  skin.i32(); const partition = skin.i32(); skin.i32(); const bones = skin.refs();
  const sp = nifio.readSkinPartition(wolf.blocks[partition].raw);
  const verts = sp.verts.map(v => ({ ...v, head: 0, jaw: 0, bone: '', most: 0 }));
  const tris = [];
  for (const part of sp.parts) {
    for (const t of part.trisCopy) tris.push(t);
    for (const v of verts) {
      for (let k = 0; k < 4; k++) {
        const w = nifio.half(v.w[k]);
        if (w <= 0) continue;
        const b = name(wolf.blocks[bones[part.bones[v.bi[k]]]].name);
        if (HEAD.test(b)) v.head += w;
        if (JAW.test(b)) v.jaw += w;
        if (w > v.most) { v.most = w; v.bone = b; }
      }
    }
  }
  return { name: name(shape.name), verts, tris, shader: wolf.blocks[shape.shader], alpha: wolf.blocks[shape.alpha] };
}

const shapes = wolf.blocks.map((b, i) => (b.type === 'BSTriShape' && b.skin >= 0 ? pieces(i) : null)).filter(Boolean);
const skin = shapes.find(s => s.name === 'WolfREDUCED');
const cards = shapes.find(s => s.name === 'Fur');
if (!skin || !cards) throw new Error('wolf.nif no longer has WolfREDUCED and Fur');

// The head's middle, from the vertices that are all head: the node the plugin turns and moves.
let n = 0; const origin = [0, 0, 0];
for (const v of skin.verts) if (v.head > 0.95) { n++; for (let k = 0; k < 3; k++) origin[k] += v.pos[k]; }
for (let k = 0; k < 3; k++) origin[k] /= n;
origin[0] = 0;

const smooth = (a, b, x) => { const t = Math.min(Math.max((x - a) / (b - a), 0), 1); return t * t * (3 - 2 * t); };
const unpack = b => b / 255 * 2 - 1;
const normalOf = v => { const n = v.n.map(unpack); const l = Math.hypot(...n) || 1; return n.map(x => x / l); };

// Where no fur grows: the eyes, the tongue, and inside the mouth (the jaw's surfaces facing up into
// it, the skull's facing down into it ahead of the hinge).
function bare(v) {
  if (/Eye$|Tongue/.test(v.bone)) return true;
  const n = normalOf(v);
  if (v.jaw > 0.5) return n[2] > 0.25;
  return v.pos[1] > hinge[1] && v.pos[2] < hinge[2] + 1.5 && n[2] < -0.25;
}
// How long the fur is there, as a share of the full length: short on the muzzle.
const furLength = v => 1 - (2 / 3) * smooth(MUZZLE[0], MUZZLE[1], v.pos[1]);

// A rigid part: triangles all on the head, each to the skull or the jaw by where most of its
// corners ride, re-indexed, placed relative to its node, pushed out along the normals by a_push,
// the neck fading out by vertex alpha.
function part(s, wantJaw, pivot, push = 0) {
  const map = new Map(), verts = [], tris = [];
  for (const t of s.tris) {
    const corners = t.map(i => s.verts[i]);
    if (corners.some(v => v.head < 0.02)) continue;
    if (corners.some(v => /Eye$/.test(v.bone))) continue;  // no eyeballs: through the glow they look stuck on
    if (push > 0 && corners.some(bare)) continue;
    const onJaw = corners.filter(v => v.jaw > 0.5).length >= 2;
    if (onJaw !== wantJaw) continue;
    tris.push(t.map(i => {
      if (!map.has(i)) {
        const v = s.verts[i];
        const normal = normalOf(v);
        const out = push * furLength(v);
        const alpha = Math.round(255 * smooth(0.1, 0.85, v.head));
        map.set(i, verts.length);
        verts.push({ pos: v.pos.map((x, k) => x - pivot[k] + normal[k] * out), btx: v.btx, uv: v.uv, n: v.n, bty: v.bty, t: v.t, btz: v.btz,
          c: [v.c ? v.c[0] : 255, v.c ? v.c[1] : 255, v.c ? v.c[2] : 255, alpha] });
      }
      return map.get(i);
    }));
  }
  return { verts, tris };
}

// pos, bitangent x, uv, normal, tangent, colour: 32 bytes; the offsets in dwords.
const desc = 8n | (4n << 8n) | (5n << 16n) | (6n << 20n) | (7n << 24n) | (0x3bn << 44n);
const identity = [1, 0, 0, 0, 1, 0, 0, 0, 1];
const NOT_SKINNED = ~0x2, VERTEX_ALPHA = 0x8, DOUBLE_SIDED = 0x10, VERTEX_COLOURS = 0x20;

function build(file, withShells, kind = KINDS.blue) {
  const SHELLS = kind.shells;
  const strings = [];
  const str = s => { let i = strings.indexOf(s); if (i < 0) { i = strings.length; strings.push(s); } return i; };
  const blocks = [];
  const add = b => blocks.push(b) - 1;
  const node = (type, label, translate, children) => ({ type, name: str(label), extra: [], controller: -1, flags: 0x8000e, translate, rotate: identity, scale: 1, collision: -1, children, effects: [] });
  const textures = t => add({ type: 'BSShaderTextureSet', textures: t });
  // The wolf's own shader, not skinned any more, both sides drawn, alpha from the vertices too.
  const lit = (from, set) => add({ ...from, raw: undefined, name: -1, extra: [], controller: -1, textureSet: set,
    flags1: (from.flags1 & NOT_SKINNED) | VERTEX_ALPHA, flags2: from.flags2 | DOUBLE_SIDED | VERTEX_COLOURS });
  const test = threshold => add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: 0x12ec, threshold });

  // Every shape gets its own shader and alpha blocks: the game keeps per-shape render state on a
  // lit shader, and one shared between shapes hung it.
  const wolfTextures = wolf.blocks[skin.shader.textureSet].textures;
  const shellTextures = [...wolfTextures]; shellTextures[0] = SHELL_TEXTURE;
  // Spectral: the unlit glow shader instead, added over the scene so the world shows through it,
  // its colour the fur's brightness read through our gradient. The skin is faint where it faces you
  // and bright at its edges; each fur shell a little fainter than the one under it.
  const glow = ({ source, tint, scale, rim }) => add({
    type: 'BSEffectShaderProperty', name: -1, extra: [], controller: -1,
    flags1: 0x80000000 | 0x10 | VERTEX_ALPHA | (rim ? 0x40 : 0),  // z-test, palette colour, vertex alpha, falloff
    flags2: DOUBLE_SIDED | VERTEX_COLOURS,
    uvOffset: [0, 0], uvScale: [1, 1], source, clamp: 3, lighting: 0, envMinLod: 0, unused: 0,
    falloff: rim ? SPECTRAL.rim : [1.0, 1.0, 1.0, 1.0],
    color: [...tint, 1], colorScale: scale, softDepth: 100, greyscale: kind.gradient
  });
  const added = threshold => add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1,
    alphaFlags: threshold > 0 ? 0x120d : 0x100d, threshold });  // src alpha + one; tested where the fur needs it
  const look = LOOK === 'natural'
    ? (from, set, threshold) => () => ({ shader: lit(from, textures(set)), alpha: test(threshold) })
    : (from, set, threshold, layer = 0) => () => ({
      shader: glow(layer > 0
        ? { source: set[0], tint: kind.furTint, scale: SPECTRAL.fur * (1 - SPECTRAL.furTips * layer / SHELLS), rim: false }
        : { source: set[0], tint: kind.skinTint, scale: kind.skin, rim: true }),
      alpha: added(layer > 0 ? threshold : 0)
    });

  function shape(label, piece, shader, alpha) {
    let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (const v of piece.verts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k]); hi[k] = Math.max(hi[k], v.pos[k]); }
    const center = lo.map((x, k) => (x + hi[k]) / 2);
    const radius = Math.max(...piece.verts.map(v => Math.hypot(v.pos[0] - center[0], v.pos[1] - center[1], v.pos[2] - center[2])));
    return add({ type: 'BSTriShape', name: str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0], rotate: identity, scale: 1, collision: -1,
      center, radius, skin: -1, shader, alpha, desc, verts: piece.verts, tris: piece.tris, particleSize: 0 });
  }

  const skull = [], jaw = [];
  const both = (label, s, make, push) => {
    const top = part(s, false, origin, push), bottom = part(s, true, hinge, push);
    if (top.tris.length) { const m = make(); skull.push(shape(`Skull${label}`, top, m.shader, m.alpha)); }
    if (bottom.tris.length) { const m = make(); jaw.push(shape(`Jaw${label}`, bottom, m.shader, m.alpha)); }
  };
  both('Skin', skin, look(skin.shader, wolfTextures, skin.alpha.threshold), 0);
  if (withShells) {
    for (let k = 1; k <= SHELLS; k++) both(`Fur${String(k).padStart(2, '0')}`, skin, look(skin.shader, shellTextures, Math.min(Math.round(240 * k / SHELLS), 250), k), kind.step * k);
  } else {
    both('FurCards', cards, look(cards.shader, wolfTextures, cards.alpha.threshold, LOOK === 'natural' ? 0 : 8), 0);
  }
  const jawNode = add(node('NiNode', 'Jaw', hinge.map((x, k) => x - origin[k]), jaw));
  const headNode = add(node('NiNode', 'Head', [0, 0, 0], [...skull, jawNode]));
  const root = add(node('BSFadeNode', 'SpectralWolfHead', [0, 0, 0], [headNode]));

  // The root first, as the game expects; references renumbered to match.
  const order = [root, ...blocks.map((_, i) => i).filter(i => i !== root)];
  const renumber = new Map(order.map((old, now) => [old, now]));
  const fix = r => (r >= 0 ? renumber.get(r) : r);
  const sorted = order.map(i => {
    const b = { ...blocks[i] };
    if (b.children) b.children = b.children.map(fix);
    if (b.type === 'BSTriShape') { b.shader = fix(b.shader); b.alpha = fix(b.alpha); }
    if (b.type === 'BSLightingShaderProperty') b.textureSet = fix(b.textureSet);
    return b;
  });
  // Nothing the game keeps per shape may be shared.
  for (const kind of ['shader', 'alpha']) {
    const used = sorted.filter(b => b.type === 'BSTriShape').map(b => b[kind]);
    if (new Set(used).size !== used.length) throw new Error(`${file}: a ${kind} block is shared between shapes`);
  }
  const nif = { headerLine: 'Gamebryo File Format, Version 20.2.0.7\n', version: 0x14020007, endian: 1, user: 12, bsver: 100,
    author: 'Fight With Me', process: '', exportScript: '', types: [], strings, groups: [], blocks: sorted, roots: [0] };
  const bytes = nifio.write(nif);
  if (!nifio.write(nifio.read(bytes)).equals(bytes)) throw new Error(`${file} does not read back the same`);
  fs.mkdirSync(outDir, { recursive: true });
  fs.writeFileSync(path.join(outDir, file), bytes);
  const tris = sorted.filter(b => b.type === 'BSTriShape').reduce((t, b) => t + b.tris.length, 0);
  console.log(`wrote ${file}: ${bytes.length} bytes, ${sorted.length} blocks, ${tris} triangles`);
}

build('SpectralWolfHead.nif', true);
build('SpectralWolfHeadPlain.nif', false);
build('SpectralWolfHeadRed.nif', true, KINDS.red);
build('SpectralWolfHeadRedPlain.nif', false, KINDS.red);
console.log(`look: ${LOOK}`);

// The gradients: 256 by 4, uncompressed BGRA, no mipmaps.
for (const { gradient: GRADIENT, stops: GRADIENT_STOPS } of Object.values(KINDS)) {
  const width = 256, height = 4;
  const header = Buffer.alloc(128);
  header.write('DDS ', 0, 'latin1');
  header.writeUInt32LE(124, 4);
  header.writeUInt32LE(0x1 | 0x2 | 0x4 | 0x8 | 0x1000, 8);  // caps, height, width, pitch, pixel format
  header.writeUInt32LE(height, 12);
  header.writeUInt32LE(width, 16);
  header.writeUInt32LE(width * 4, 20);
  header.writeUInt32LE(32, 76);                               // pixel format size
  header.writeUInt32LE(0x40 | 0x1, 80);                       // RGB with alpha
  header.writeUInt32LE(32, 88);
  header.writeUInt32LE(0x00ff0000, 92);
  header.writeUInt32LE(0x0000ff00, 96);
  header.writeUInt32LE(0x000000ff, 100);
  header.writeUInt32LE(0xff000000, 104);
  header.writeUInt32LE(0x1000, 108);                          // a texture
  const pixels = Buffer.alloc(width * height * 4);
  for (let x = 0; x < width; x++) {
    const u = x / (width - 1);
    let i = 0; while (i < GRADIENT_STOPS.length - 2 && u > GRADIENT_STOPS[i + 1][0]) i++;
    const [u0, c0] = GRADIENT_STOPS[i], [u1, c1] = GRADIENT_STOPS[i + 1];
    const t = Math.min(Math.max((u - u0) / (u1 - u0), 0), 1);
    const c = c0.map((v, k) => Math.round(v + (c1[k] - v) * t));
    for (let y = 0; y < height; y++) pixels.set([c[2], c[1], c[0], 255], (y * width + x) * 4);
  }
  const file = path.join(__dirname, '..', '..', 'assets', GRADIENT.replace(/\\/g, '/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.concat([header, pixels]));
  console.log(`wrote ${GRADIENT}`);
}

// For the bite: the head's length along +Y (skull back to muzzle tip), and how far round it is.
let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
for (const v of skin.verts) if (v.head > 0.95) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k] - origin[k]); hi[k] = Math.max(hi[k], v.pos[k] - origin[k]); }
console.log(`origin (head middle) ${origin.map(x => x.toFixed(2)).join(', ')}; jaw hinge from it ${hinge.map((x, k) => (x - origin[k]).toFixed(2)).join(', ')}`);
console.log(`head extent from origin: x ${lo[0].toFixed(1)}..${hi[0].toFixed(1)}, y ${lo[1].toFixed(1)}..${hi[1].toFixed(1)}, z ${lo[2].toFixed(1)}..${hi[2].toFixed(1)}`);
