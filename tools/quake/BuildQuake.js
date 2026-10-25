// Builds meshes\FightWithMe\Earthshaker.nif, Earthshaker's slam: a flash where it lands, cracks and a
// ring of dust running out over the ground, clouds of dust thrown up, and rocks bursting out of the
// earth, the game's own boulders (meshes\landscape\rocks\rockl, rockm and rocks), lit and textured as
// they are in the world, so they match whatever rock textures the load order has. Each rock is its own
// copy with its own shader, brought to Quake::kRock across with its foot just in the ground, and every
// node starts too small to see: the plugin poses them all (src/Quake.cpp has the numbers).
//
//   node tools\quake\BuildQuake.js

const path = require('path');
const nifio = require('../wolfhead/nifio.js');
const { open } = require('../wolfhead/bsa.js');
const { random, piece, band, disc, sprite, meshWriter, FX, textured } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const DATA = 'X:/!--- Main/Documents/Nolvus/STOCK GAME/Data';
const R = 300;     // Quake::kMeshRadius
const ROCK = 100;  // Quake::kRock

// How strongly each glow and the dust show (their shaders' colour scale). The colour comes from the plugin.
const LOOK = { flash: 1.2, cracks: 1.0, ring: 0.9, dust: 1.0 };

// --- The game's boulders --------------------------------------------------------------------

const archives = ['Skyrim - Meshes0.bsa', 'Skyrim - Meshes1.bsa'].map(a => open(`${DATA}/${a}`));
function vanilla(file) {
  for (const bsa of archives) {
    const hit = bsa.files.find(f => f.path.toLowerCase() === file.toLowerCase());
    if (hit) return nifio.read(bsa.read(hit));
  }
  throw new Error(`${file} is not in the game's archives`);
}
const BOULDERS = {
  large: ['rockl01', 'rockl03'].map(n => vanilla(`meshes\\landscape\\rocks\\${n}.nif`)),
  medium: ['rockm01', 'rockm02'].map(n => vanilla(`meshes\\landscape\\rocks\\${n}.nif`)),
  small: ['rocks01', 'rocks02'].map(n => vanilla(`meshes\\landscape\\rocks\\${n}.nif`))
};

// A copy of a boulder's shape, shader and textures as new blocks in our mesh, its own transform
// bringing it to ROCK across, centred, its foot a little in the ground.
function boulder(m, label, nif) {
  const shapeIndex = nif.blocks.findIndex(b => b.type === 'BSTriShape');
  const src = nif.blocks[shapeIndex], shader = nif.blocks[src.shader], textures = nif.blocks[shader.textureSet];
  const lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
  for (const v of src.verts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k]); hi[k] = Math.max(hi[k], v.pos[k]); }
  const k = ROCK / Math.max(hi[0] - lo[0], hi[1] - lo[1]);
  const set = m.addBlock({ type: 'BSShaderTextureSet', textures: [...textures.textures] });
  const lit = m.addBlock({ ...shader, name: -1, extra: [], controller: -1, textureSet: set, tail: shader.tail ? Buffer.from(shader.tail) : Buffer.alloc(0) });
  return m.addBlock({
    ...src, name: m.str(label), extra: [], controller: -1, collision: -1, skin: -1, shader: lit, alpha: -1,
    translate: [-(lo[0] + hi[0]) / 2 * k, -(lo[1] + hi[1]) / 2 * k, -lo[2] * k - 0.15 * (hi[2] - lo[2]) * k],
    rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: k,
    verts: src.verts.map(v => ({ ...v })), tris: src.tris.map(t => [...t])
  });
}

// --- The mesh -------------------------------------------------------------------------------

const m = meshWriter();
// A part's node, too small to see until the plugin first poses it (the lit rocks cannot be faded in).
const hidden = (label, children) => m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0],
  rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 0.0001, collision: -1, children, effects: [] });
const parts = [];

// A sprite of a game texture, kept square to the camera.
function turnedSprite(label, size, look, texture, atlas = {}) {
  const quad = piece();
  sprite(quad, size, atlas);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured(look, texture))]);
}
// The flash where it lands.
parts.push(hidden('Flash', [turnedSprite('FlashGlow', 200, { scale: LOOK.flash }, FX.glowSpot)]));
// Cracks over the ground round the slam, dark in it, fading at their edge.
{
  const ground = piece();
  disc(ground, R * 0.85, 2, r => (r < 1 ? 0.95 * (1 - Math.pow(r, 3)) : 0));
  parts.push(hidden('Cracks', [m.shape('CrackLines', ground, textured({ scale: LOOK.cracks, normal: true }, FX.cracks, [1.8, 1.8]))]));
}
// The ring of dust the shock pushes out over the ground.
{
  const ring = piece();
  band(ring, R * 0.82, R * 1.03, 3, [0, 0.4, 1, 0.6, 0]);
  parts.push(hidden('DustRing', [m.shape('DustRingCloud', ring, textured({ scale: LOOK.ring, normal: true }, FX.cloud, [3, 1]))]));
}
// Clouds of dust: puffs of the game's own smoke, each a different one, turned to the camera.
for (let k = 0; k < 8; k++) {
  parts.push(hidden(`Dust${k}`, [turnedSprite(`Dust${k}Cloud`, 170, { scale: LOOK.dust, normal: true }, FX.smoke, { cols: 4, rows: 4, cell: k * 2 })]));
}
// The rocks: big ones bursting up in rings, small ones flung out of the middle, and the grand third
// rank's giants.
for (let k = 0; k < 12; k++) {
  const from = k % 3 === 2 ? BOULDERS.medium[k % 2] : BOULDERS.large[k % 2];
  parts.push(hidden(`Slab${String(k).padStart(2, '0')}`, [boulder(m, `Slab${String(k).padStart(2, '0')}Rock`, from)]));
}
for (let k = 0; k < 16; k++) {
  parts.push(hidden(`Chunk${String(k).padStart(2, '0')}`, [boulder(m, `Chunk${String(k).padStart(2, '0')}Rock`, BOULDERS.small[k % 2])]));
}
for (let k = 0; k < 5; k++) {
  parts.push(hidden(`Spire${k}`, [boulder(m, `Spire${k}Rock`, BOULDERS.large[k % 2])]));
}
m.write(path.join(OUT, 'meshes', 'FightWithMe', 'Earthshaker.nif'), m.node('BSFadeNode', 'Earthshaker', [m.node('NiNode', 'Quake', parts)]));
