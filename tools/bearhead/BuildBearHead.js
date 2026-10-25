// Taunt: a giant amber bear head cut from Skyrim’s own bear, with an animated jaw.
// Fur shells reference our generated SpectralFur.dds; the plain variant uses Skyrim skin.
// Four distinct paths permit four simultaneous taunts. No third-party textures are bundled.
// node tools/bearhead/BuildBearHead.js [Skyrim Data folder]

const fs = require('fs');
const path = require('path');
const { open } = require('../wolfhead/bsa.js');
const nifio = require('../wolfhead/nifio.js');

const data = process.argv.slice(2).find(a => !a.startsWith('--')) || 'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const outDir = path.join(__dirname, '..', '..', 'assets', 'meshes', 'FightWithMe');

// --beast=sabre / --beast=dragon reuse the same spectral head writer for War Cry.
const BEAST = process.argv.find(a => a.startsWith('--beast='))?.split('=')[1] || 'bear';
if (!['bear','sabre','dragon','alduin'].includes(BEAST)) throw new Error('Unknown beast');
const BREATH = process.argv.includes('--breath'), ALDUIN = BEAST === 'alduin';
const WAR = BEAST !== 'bear', DRAGON = BEAST === 'dragon' || ALDUIN;
const PREFIX = BREATH ? (ALDUIN ? 'DragonBreathAlduin' : 'DragonBreathHead') : WAR ? (DRAGON ? 'WarCryDragon' : 'WarCrySabre') : 'SpectralBearHead';
const SHELLS = DRAGON ? 0 : 16;
const LOOK = process.argv.includes('--natural') ? 'natural' : 'spectral';
// The fur's brightness read through an original gradient: dark umber to amber to ivory.
const GRADIENT = `textures\\FightWithMe\\${BREATH ? 'DragonBreathBlueGradient' : WAR ? 'WarCryGoldGradient' : 'SpectralBearGradient'}.dds`;
// How bright each part glows: every fur shell adds its light over the last, so they stay faint.
const SPECTRAL = { skin: 0.75, rim: [1.0, 0.0, 0.25, 1.0], fur: 0.16, furTips: 0.6 };  // furTips: share lost by the outermost shell
const RED_GRADIENT = `textures\\FightWithMe\\${WAR ? 'WarCryRedGradient' : 'SpectralBearRedGradient'}.dds`;
const RED_STOPS = [[0, [65, 1, 2]], [0.15, [160, 3, 5]], [0.35, [220, 6, 8]], [0.65, [245, 12, 12]], [1, [250, 24, 20]]];
const GRADIENT_STOPS = BREATH ? [[0,[4,15,40]],[0.08,[15,65,155]],[0.25,[45,150,245]],[0.55,[100,190,255]],[1,[160,220,255]]] : [[0, [18, 11, 1]], [0.3, [110, 65, 5]], [0.55, [210, 150, 18]], [0.8, [250, 212, 65]], [1, [255, 242, 155]]];
const SHELL_STEP = 0.16;  // how much further out each shell sits, in the bear's own units
const SHELL_TEXTURE = 'textures\\FightWithMe\\SpectralFur.dds';
const MUZZLE = [33, 48];  // fur shortens from the cheeks to the nose, to a third

const meshes = open(path.join(data, 'Skyrim - Meshes0.bsa'));
const model = BEAST === 'sabre' ? 'sabrecat' : DRAGON ? 'dragon' : BEAST;
const source = meshes.files.find(f => f.path.toLowerCase() === `meshes\\actors\\${model}\\character assets\\${ALDUIN ? 'alduin' : model}.nif`);
if (!source) throw new Error('bear.nif not found in Skyrim - Meshes0.bsa');
const bear = nifio.read(meshes.read(source));
const name = i => (i >= 0 ? bear.strings[i] : '');

// Bones at rest, in the mesh's space (the skinned vertices are stored in the same space).
const parent = new Map();
bear.blocks.forEach((b, i) => { if (b.children) for (const c of b.children) if (c >= 0) parent.set(c, i); });
const mul = (a, b) => { const r = []; for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) { let s = 0; for (let k = 0; k < 3; k++) s += a[i * 3 + k] * b[k * 3 + j]; r.push(s); } return r; };
const apply = (m, v) => [0, 1, 2].map(i => m[i * 3] * v[0] + m[i * 3 + 1] * v[1] + m[i * 3 + 2] * v[2]);
function rest(i) {
  const b = bear.blocks[i];
  if (!parent.has(i)) return { r: b.rotate, t: b.translate, s: b.scale };
  const p = rest(parent.get(i));
  return { r: mul(p.r, b.rotate), t: apply(p.r, b.translate).map((x, k) => x * p.s + p.t[k]), s: p.s * b.scale };
}
const bone = n => { const i = bear.blocks.findIndex(b => b.type === 'NiNode' && name(b.name) === n); if (i < 0) throw new Error(`no bone ${n}`); return rest(i).t; };
const hinge = bone(BEAST === 'sabre' ? 'Sabrecat_Head[jaw]' : 'NPC Jaw');

const HEAD = /Head|Jaw|Tongue|Ear(?!m)|Eyebrow|Brow|Lip|Nose/i;
const JAW = /Jaw|Tongue|Lower.*Lip/i;
// "Ear" must never match "Forearm": that used to import legs and dragon wings.
for (const limb of ['NPC LForearm1','NPC RForearm2','Sabrecat_LeftForearm[LFar]'])
  if (HEAD.test(limb)) throw new Error('Head extraction includes a limb: '+limb);

// A skinned shape's vertices with how much of each rides on the head and on the jaw, its triangles,
// and the shader and alpha settings it is drawn with.
function pieces(shapeIndex) {
  const shape = bear.blocks[shapeIndex];
  const skin = new nifio.Reader(bear.blocks[shape.skin].raw);
  skin.i32(); const partition = skin.i32(); skin.i32(); const bones = skin.refs();
  const sp = nifio.readSkinPartition(bear.blocks[partition].raw);
  const world = rest(shapeIndex);
  const verts = sp.verts.map(v => ({ ...v,
    pos: WAR ? apply(world.r, v.pos).map((x,k)=>x*world.s+world.t[k]) : v.pos,
    head: 0, jaw: 0, bone: '', most: 0 }));
  const tris = [];
  // SSE's shared vertex buffer indexes the skin instance's global bone list. The
  // partition palettes apply to part.boneIdx only, not v.bi (the bear has two partitions).
  for (const part of sp.parts) for (const t of part.trisCopy) tris.push(t);
  for (const v of verts) {
    for (let k = 0; k < 4; k++) {
      const w = nifio.half(v.w[k]);
      if (w <= 0) continue;
      const b = name(bear.blocks[bones[v.bi[k]]].name);
      if (HEAD.test(b)) v.head += w;
      if (JAW.test(b)) v.jaw += w;
      if (w > v.most) { v.most = w; v.bone = b; }
    }
  }
  return { name: name(shape.name), verts, tris, shader: bear.blocks[shape.shader], alpha: bear.blocks[shape.alpha] };
}

const shapes = bear.blocks.map((b, i) => (b.type === 'BSTriShape' && b.skin >= 0 ? pieces(i) : null)).filter(Boolean).filter(s => !ALDUIN || !bear.blocks[s.shader.textureSet].textures.some(t=>/alduineyes/i.test(t)));
const skin = WAR ? shapes.reduce((a,b) => a.verts.filter(v=>v.head>.95).length > b.verts.filter(v=>v.head>.95).length ? a : b) : shapes.find(s => s.name === 'BearNew');
if (!skin) throw new Error('bear.nif no longer has BearNew');

// Normalize the animal heads to the same visual scale as the bear before making rigid parts.
if (WAR) {
  const head = skin.verts.filter(v=>v.head>.95);
  const width = Math.max(...head.map(v=>v.pos[0]))-Math.min(...head.map(v=>v.pos[0]));
  const scale = (DRAGON ? 62 : 52) / width;
  for (const shape of shapes) for (const v of shape.verts) v.pos = v.pos.map(x=>x*scale);
  for (let i=0;i<3;i++) hinge[i]*=scale;
}
// The head's middle, from the vertices that are all head: the node the plugin turns and moves.
let n = 0; const origin = [0, 0, 0];
for (const v of skin.verts) if (v.head > 0.95) { n++; for (let k = 0; k < 3; k++) origin[k] += v.pos[k]; }
for (let k = 0; k < 3; k++) origin[k] /= n;
origin[0] = 0;

// Skyrim's dragon eyes share the teeth/tongue mesh and are weighted to Head, not eye bones.
// Remove their four disconnected surfaces (two globes and two caps), preserving the brows.
if (DRAGON && !ALDUIN) {
  const face = shapes.find(s => s.name === 'Dragon:1');
  if (!face) throw new Error('Dragon eye-bearing surface is missing');
  const links = face.verts.map(() => []), visited = new Set(), eyes = new Set();
  for (const tri of face.tris) for (const i of tri) for (const j of tri) links[i].push(j);
  let components = 0;
  for (let i = 0; i < face.verts.length; ++i) {
    if (visited.has(i)) continue;
    const pending = [i], ids = []; visited.add(i);
    while (pending.length) {
      const k = pending.pop(); ids.push(k);
      for (const j of links[k]) if (!visited.has(j)) { visited.add(j); pending.push(j); }
    }
    const eye = ids.length >= 20 && ids.length <= 40 && ids.every(k => {
      const v = face.verts[k], x = Math.abs(v.pos[0]), y = v.pos[1] - origin[1], z = v.pos[2] - origin[2];
      return v.bone === 'NPC Head' && x > 9 && x < 14 && y > 2 && y < 9 && z > 2 && z < 7;
    });
    if (eye) { ++components; for (const k of ids) eyes.add(k); }
  }
  if (components !== 4 || eyes.size !== 112) throw new Error('Dragon eye topology changed; inspect before exporting');
  face.tris = face.tris.filter(tri => !tri.some(i => eyes.has(i)));
  console.log(`Removed ${components} dragon eye surfaces (${eyes.size} vertices).`);
}

const smooth = (a, b, x) => { const t = Math.min(Math.max((x - a) / (b - a), 0), 1); return t * t * (3 - 2 * t); };
const unpack = b => b / 255 * 2 - 1;
const normalOf = v => { const n = v.n.map(unpack); const l = Math.hypot(...n) || 1; return n.map(x => x / l); };

// Where no fur grows: the eyes, the tongue, and inside the mouth (the jaw's surfaces facing up into
// it, the skull's facing down into it ahead of the hinge).
function bare(v) {
  if (/Eye\]|Eye$|Tongue/i.test(v.bone)) return true;
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
    if (corners.some(v => /Eye\]|Eye$/i.test(v.bone))) continue;  // no eyeballs: through the glow they look stuck on
    if (push > 0 && corners.some(bare)) continue;
    const onJaw = corners.filter(v => v.jaw > 0.5).length >= 2;
    if (onJaw !== wantJaw) continue;
    tris.push(t.map(i => {
      if (!map.has(i)) {
        const v = s.verts[i];
        const normal = normalOf(v);
        const out = push * furLength(v);
        const alpha = Math.round((v.c ? v.c[3] : 255) * smooth(0.1, 0.85, v.head));
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

function build(file, withShells, grand = false) {
  const palette = grand ? RED_GRADIENT : GRADIENT;
  const gain = BREATH ? (grand ? 2.4 : 1.8) : DRAGON ? 0.85 : grand ? 0.55 : 1;
  const strings = [];
  const str = s => { let i = strings.indexOf(s); if (i < 0) { i = strings.length; strings.push(s); } return i; };
  const blocks = [];
  const add = b => blocks.push(b) - 1;
  const node = (type, label, translate, children) => ({ type, name: str(label), extra: [], controller: -1, flags: 0x8000e, translate, rotate: identity, scale: 1, collision: -1, children, effects: [] });
  const textures = t => add({ type: 'BSShaderTextureSet', textures: t });
  // The bear's own shader, not skinned any more, both sides drawn, alpha from the vertices too.
  const lit = (from, set) => add({ ...from, raw: undefined, name: -1, extra: [], controller: -1, textureSet: set,
    flags1: (from.flags1 & NOT_SKINNED) | VERTEX_ALPHA, flags2: from.flags2 | DOUBLE_SIDED | VERTEX_COLOURS });
  const test = threshold => add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: 0x12ec, threshold });

  // Every shape gets its own shader and alpha blocks: the game keeps per-shape render state on a
  // lit shader, and one shared between shapes hung it.
  const bearTextures = bear.blocks[skin.shader.textureSet].textures;
  const shellTextures = [...bearTextures]; shellTextures[0] = SHELL_TEXTURE;
  // Spectral: the unlit glow shader instead, added over the scene so the world shows through it,
  // its colour the fur's brightness read through our gradient. The skin is faint where it faces you
  // and bright at its edges; each fur shell a little fainter than the one under it.
  const glow = ({ source, tint, scale, rim }) => add({
    type: 'BSEffectShaderProperty', name: -1, extra: [], controller: -1,
    flags1: 0x80000000 | 0x10 | VERTEX_ALPHA | (rim ? 0x40 : 0),  // z-test, palette colour, vertex alpha, falloff
    flags2: DOUBLE_SIDED | VERTEX_COLOURS,
    uvOffset: [0, 0], uvScale: [1, 1], source, clamp: 3, lighting: 0, envMinLod: 0, unused: 0,
    falloff: rim ? (DRAGON ? [1, 0, 0.70, 1] : SPECTRAL.rim) : [1.0, 1.0, 1.0, 1.0],
    color: [0, 0, 0, 1], colorScale: scale * gain, softDepth: 100, greyscale: palette
  });
  const added = threshold => add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1,
    alphaFlags: threshold > 0 ? 0x120d : 0x100d, threshold });  // src alpha + one; tested where the fur needs it
  const look = LOOK === 'natural'
    ? (from, set, threshold) => () => ({ shader: lit(from, textures(set)), alpha: test(threshold) })
    : (from, set, threshold, layer = 0) => () => ({
      shader: glow(layer > 0
        ? { source: set[0], tint: [1.0, 1.0, 1.0], scale: SPECTRAL.fur * (1 - SPECTRAL.furTips * layer / SHELLS), rim: false }
        : { source: set[0], tint: [1.0, 1.0, 1.0], scale: SPECTRAL.skin * (grand ? 1.55 : 1), rim: true }),
      // A translucent crimson body preserves the hue against bright skies/ENB bloom;
      // only its fine fur layers add light. An entirely additive head washes out.
      alpha: grand && layer === 0
        ? add({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: 0x10ed, threshold: 0 })
        : added(layer > 0 ? threshold : 0)
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
    if (bottom.tris.length) {
      const m = make();
      // The dark chin needs more body than the brow to read through the spectral glow.
      if (LOOK === 'spectral' && label === 'Skin' && !WAR) {
        blocks[m.shader].colorScale *= 1.3;
        blocks[m.shader].falloff = [1, 0, 0.35, 1];
      }
      jaw.push(shape(`Jaw${label}`, bottom, m.shader, m.alpha));
    }
  };
  if (WAR) {
    for (const [i, surface] of shapes.entries()) {
      const set = bear.blocks[surface.shader.textureSet].textures;
      both(`Skin${i}`, surface, look(surface.shader, set, surface.alpha?.threshold || 0), 0);
    }
  } else both('Skin', skin, look(skin.shader, bearTextures, skin.alpha.threshold), 0);
  if (withShells) {
    for (let k = 1; k <= SHELLS; k++) both(`Fur${String(k).padStart(2, '0')}`, skin, look(skin.shader, shellTextures, Math.min(15 * k, 250), k), SHELL_STEP * k);
  }
  const jawNode = add(node('NiNode', 'Jaw', hinge.map((x, k) => x - origin[k]), jaw));
  const headNode = add(node('NiNode', 'Head', [0, 0, 0], [...skull, jawNode]));
  const waves = [];
  for (let k = 0; k < 3; k++) {
    const { piece, band } = require('../fx/meshkit.js');
    const ring = piece(); band(ring, 0.84, 1, 0, [0, 0.65, 1, 0], 64);
    // Face forward (+Y), instead of lying on the ground.
    for (const v of ring.verts) { v.pos = [v.pos[0], 0, v.pos[1]]; v.n = [128, 0, 128]; }
    const shader = glow({ source: 'textures\\FightWithMe\\Glow.dds', tint: [1,1,1], scale: 0.8, rim: false });
    blocks[shader].flags1 &= ~0x10; blocks[shader].greyscale = '';
    waves.push(add(node('NiNode', 'Wave' + k, [0,0,0], [shape('RoarRing' + k, ring, shader, added(0))])));
  }
  const spell = [];
  if (BREATH) {
    const { piece, sprite } = require('../fx/meshkit.js');
    const seal=piece();sprite(seal,2);
    for(const v of seal.verts){v.pos=[v.pos[0],0,v.pos[1]];v.n=[128,0,128];}
    const shader=glow({source:'textures\\FightWithMe\\RejuvenationSeal.dds',tint:[1,1,1],scale:.55,rim:false});
    spell.push(add(node('NiNode','SummoningRune',[0,0,0],[shape('DragonSeal',seal,shader,added(0))])));
  }
  const root = add(node('BSFadeNode', PREFIX, [0, 0, 0], [headNode, ...waves, ...spell]));

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

for (let copy = 0; copy < 4; copy++) {
  const suffix = copy ? String(copy + 1) : '';
  if (WAR) {
    build(`${PREFIX}${suffix}.nif`, !DRAGON, BREATH ? ALDUIN : DRAGON);
    if (BEAST === 'sabre') build(`${PREFIX}Red${suffix}.nif`, true, true);
  } else {
    build(`SpectralBearHead${suffix}.nif`, true);
    build(`SpectralBearHeadPlain${suffix}.nif`, false);
    build(`SpectralBearHeadRed${suffix}.nif`, true, true);
    build(`SpectralBearHeadRedPlain${suffix}.nif`, false, true);
  }
}
console.log(`look: ${LOOK}`);

// Separate palettes: rank III never samples an ivory/yellow highlight.
for (const [gradient, stops] of [[GRADIENT, GRADIENT_STOPS], [RED_GRADIENT, RED_STOPS]]) {
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
    let i = 0; while (i < stops.length - 2 && u > stops[i + 1][0]) i++;
    const [u0, c0] = stops[i], [u1, c1] = stops[i + 1];
    const t = Math.min(Math.max((u - u0) / (u1 - u0), 0), 1);
    const c = c0.map((v, k) => Math.round(v + (c1[k] - v) * t));
    for (let y = 0; y < height; y++) pixels.set([c[2], c[1], c[0], 255], (y * width + x) * 4);
  }
  const file = path.join(__dirname, '..', '..', 'assets', gradient.replace(/\\/g, '/'));
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.concat([header, pixels]));
  console.log(`wrote ${gradient}`);
}

// The head's extent along +Y (skull back to muzzle tip), and how far round it is.
let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
for (const v of skin.verts) if (v.head > 0.95) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k] - origin[k]); hi[k] = Math.max(hi[k], v.pos[k] - origin[k]); }
console.log(`origin (head middle) ${origin.map(x => x.toFixed(2)).join(', ')}; jaw hinge from it ${hinge.map((x, k) => (x - origin[k]).toFixed(2)).join(', ')}`);
console.log(`head extent from origin: x ${lo[0].toFixed(1)}..${hi[0].toFixed(1)}, y ${lo[1].toFixed(1)}..${hi[1].toFixed(1)}, z ${lo[2].toFixed(1)}..${hi[2].toFixed(1)}`);
