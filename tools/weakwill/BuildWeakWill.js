// A spectral overlay made from the player's local Skyrim heavy steel helmet. The source mesh
// and textures remain untouched. Vanilla texture paths are referenced, never redistributed.
// node tools/weakwill/BuildWeakWill.js [Skyrim Data folder]
const fs = require('fs'), path = require('path');
const { open } = require('../wolfhead/bsa.js');
const nifio = require('../wolfhead/nifio.js');
const { piece, ribbon, sprite, meshWriter, FX, random } = require('../fx/meshkit.js');
const DATA = process.argv[2] || 'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const OUT = path.resolve(__dirname, '../../assets');
const GRADIENT = 'textures\\FightWithMe\\WeakWillSteelGradient.dds';
const EBONY = 'textures\\FightWithMe\\WeakWillEbonyGradient.dds';
const archive = open(path.join(DATA, 'Skyrim - Meshes0.bsa'));
function helmet(grand) {
const SOURCE = grand ? 'meshes\\armor\\ebony\\m\\helmet_1.nif' : 'meshes\\armor\\steel\\helmet_1.nif';
const entry = archive.files.find(f => f.path.toLowerCase() === SOURCE);
if (!entry) throw new Error(`Local Skyrim is missing ${SOURCE}`);
const original = nifio.read(archive.read(entry));
const source = original.blocks.find(b => b.type === 'BSTriShape' && original.strings[b.name] === (grand ? 'Helmet' : 'SteelHelmetHeavyDouble'));
if (!source || source.skin < 0) throw new Error('Skyrim steel helmet shape changed');
const skin = new nifio.Reader(original.blocks[source.skin].raw); skin.i32(); const partition = skin.i32();
const data = nifio.readSkinPartition(original.blocks[partition].raw);
const texture = original.blocks[original.blocks[source.shader].textureSet].textures[0];
const tris = data.parts.flatMap(part => part.trisCopy);
return { data, texture, tris };
}
const normal = v => { const n = v.n.map(b => b / 255 * 2 - 1), length = Math.hypot(...n) || 1; return n.map(x => x / length); };
const clamp = x => Math.max(0, Math.min(1, x));
function steel({ data, tris }, push = 0) {
  return { tris: tris.map(t => [...t]), verts: data.verts.map(v => {
    const n = normal(v), edge = .68 + .32 * clamp((v.pos[2] + 14) / 4);
    return { pos: v.pos.map((x, k) => x + n[k] * push), btx: v.btx, uv: [...v.uv], n: [...v.n], bty: v.bty,
      t: [...v.t], btz: v.btz, c: [255, 255, 255, Math.round(edge * 255)] };
  }) };
}
const identity = [1, 0, 0, 0, 1, 0, 0, 0, 1];
const desc = 8n | (4n << 8n) | (5n << 16n) | (6n << 20n) | (7n << 24n) | (0x3bn << 44n);
function build(grand) {
  const source = helmet(grand), texture = source.texture;
  const m = meshWriter(), nodes = [];
  function hidden(name, children) {
    return m.addBlock({ type: 'NiNode', name: m.str(name), extra: [], controller: -1, flags: 0x8000e,
      translate: [0, 0, 0], rotate: identity, scale: .0001, collision: -1, children, effects: [] });
  }
  function helmetShape(label, geometry, rim) {
    const shader = m.addBlock({ type: 'BSEffectShaderProperty', name: -1, extra: [], controller: -1,
      // Only the body needs palette remapping. A palette on the rim overrides RGB fading.
      flags1: 0x80000000 | (rim ? 0 : 0x10) | 0x8 | 0x40, flags2: 0x10 | 0x20,
      uvOffset: [0, 0], uvScale: [1, 1], source: texture, clamp: 3, lighting: 0, envMinLod: 0, unused: 0,
      falloff: rim ? [1, 0, .025, 1] : [1, 0, .95, .72], color: [1, 1, 1, 0],
      colorScale: rim ? 1.25 : 1.45, softDepth: 20, greyscale: rim ? '' : (grand ? EBONY : GRADIENT) });
    const alpha = m.addBlock({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: rim ? 0x100d : 0x10ed, threshold: 0 });
    const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
    for (const v of geometry.verts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k]); hi[k] = Math.max(hi[k], v.pos[k]); }
    const center = lo.map((x, k) => (x + hi[k]) / 2), radius = Math.max(...geometry.verts.map(v => Math.hypot(...v.pos.map((x, k) => x - center[k]))));
    return m.addBlock({ type: 'BSTriShape', name: m.str(label), extra: [], controller: -1, flags: 0x8000e,
      translate: [0, 0, 0], rotate: identity, scale: 1, collision: -1, center, radius, skin: -1, shader, alpha,
      desc, verts: geometry.verts, tris: geometry.tris, particleSize: 0 });
  }
  nodes.push(hidden('Helmet', [helmetShape(grand ? 'EbonyHelmet' : 'SteelHelmet', steel(source), false)]));
  nodes.push(hidden('Rim', [helmetShape('HelmetEdge', steel(source, .10), true)]));
  const mist = piece();
  for (let k = 0; k < (grand ? 4 : 2); k++) {
    const phase = k * Math.PI * 2 / (grand ? 4 : 2) + .45;
    const points = Array.from({ length: 25 }, (_, i) => {
      const f = i / 24, angle = phase + .46 * Math.sin(f * 3.8);
      const radius = 13 + 2 * Math.sin(f * Math.PI);
      return [Math.cos(angle) * radius, Math.sin(angle) * radius, -18 + f * 37];
    });
    ribbon(mist, points, f => 3 + 7 * Math.sin(f * Math.PI), f => .28 * Math.pow(Math.sin(f * Math.PI), 1.7), { v: f => f * 1.6 });
  }
  nodes.push(hidden('Mist', [m.shape('ColdSteelWisps', mist, { scale: 1.8, texture: FX.frostCloud, soft: 12 })]));
  const motes = [], rand = random(3274);
  for (let k = 0; k < (grand ? 28 : 16); k++) {
    const p = piece(); sprite(p, .35 + .5 * rand(), { alpha: .4 + .5 * rand() });
    const angle = rand() * Math.PI * 2, radius = 12 + 7 * rand();
    const shape = m.shape(`SteelMote${k}`, p, { scale: 1.1, texture: FX.glowSpot, soft: 5 });
    const billboard = m.billboard(`SteelMoteFacing${k}`, [shape]);
    motes.push(m.addBlock({ type: 'NiNode', name: m.str(`SteelMoteAt${k}`), extra: [], controller: -1, flags: 0x8000e,
      translate: [Math.cos(angle) * radius, Math.sin(angle) * radius, -14 + 34 * rand()], rotate: identity, scale: 1, collision: -1, children: [billboard], effects: [] }));
  }
  nodes.push(hidden('Motes', motes));
  const root = m.node('BSFadeNode', 'WeakWillHelmet', nodes);
  for (let copy = 0; copy < 4; copy++) m.write(path.join(OUT, 'meshes/FightWithMe', `WeakWillHelmet${grand ? 'Grand' : ''}${copy ? copy + 1 : ''}.nif`), root);
}

// Original palette: cool silver with readable dark engraving, capped well below white so ENB
// bloom cannot turn the whole helmet into a featureless yellow-white shape.
// The original albedo's plate mostly occupies 0.10–0.30, so spend the useful silver range
// there. Mapping only 0.7–1.0 to silver would make this unlit helmet look almost black.
const silver = [[0, [32, 45, 60]], [.08, [86, 108, 127]], [.18, [164, 184, 202]],
  [.30, [207, 221, 232]], [.5, [220, 231, 241]], [1, [225, 235, 243]]];
const violet = [[0, [24, 9, 40]], [.04, [65, 26, 94]], [.10, [113, 48, 160]],
  [.23, [177, 96, 220]], [.5, [212, 153, 240]], [1, [229, 187, 249]]];
function palette(stops, name) {
const header = Buffer.alloc(128), width = 256, height = 4;
header.write('DDS ', 0, 'latin1'); header.writeUInt32LE(124, 4); header.writeUInt32LE(0x100f, 8);
header.writeUInt32LE(height, 12); header.writeUInt32LE(width, 16); header.writeUInt32LE(width * 4, 20);
header.writeUInt32LE(32, 76); header.writeUInt32LE(0x41, 80); header.writeUInt32LE(32, 88);
header.writeUInt32LE(0x00ff0000, 92); header.writeUInt32LE(0x0000ff00, 96); header.writeUInt32LE(0x000000ff, 100); header.writeUInt32LE(0xff000000, 104); header.writeUInt32LE(0x1000, 108);
const pixels = Buffer.alloc(width * height * 4);
for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
  const f = x / (width - 1); let i = 1; while (i + 1 < stops.length && f > stops[i][0]) i++;
  const a = stops[i - 1], b = stops[i], t = (f - a[0]) / (b[0] - a[0]), rgb = a[1].map((v, k) => Math.round(v + (b[1][k] - v) * t));
  const at = (y * width + x) * 4; pixels[at] = rgb[2]; pixels[at + 1] = rgb[1]; pixels[at + 2] = rgb[0]; pixels[at + 3] = 255;
}
const gradientPath = path.join(OUT, name.replace(/\\/g, '/'));
fs.mkdirSync(path.dirname(gradientPath), { recursive: true }); fs.writeFileSync(gradientPath, Buffer.concat([header, pixels]));
}
palette(silver, GRADIENT); palette(violet, EBONY);
build(false); build(true);
