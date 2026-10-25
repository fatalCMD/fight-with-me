// The shared kit for Fight With Me's effect meshes made from code (the Elemental Burst blasts, the
// Magnus orb, Earthshaker's slam, the healer's circle): small vector maths, seeded scatter and noise,
// glowing geometry (balls, bands, discs, walls, domes, soft ribbons, crystals, jagged lines, sprites that
// turn to the camera) and a writer that turns a node tree into a NIF whose every shape has its own effect
// shader and alpha blocks. The textures are the game's own effect textures (FX), so the load order's
// effect texture mods dress them too; every glow is a soft effect, fading where it meets the world.

const fs = require('fs');
const path = require('path');
const nifio = require('../wolfhead/nifio.js');

const GLOW = 'textures\\FightWithMe\\Glow.dds';
// The game's effect textures, and how their atlases are laid out where they are atlases.
const FX = {
  glowSpot: 'textures\\effects\\FXGlowSpotLinearAlpha.dds',   // a soft round glow
  glowSoft: 'textures\\effects\\GlowSoft01.dds',              // a softer, wider one
  fireball: 'textures\\effects\\FireballQuad01.dds',          // four puffs of flame, 2 by 2, in grey
  fireWave: 'textures\\effects\\FireWave01.dds',              // a sheet of rising flame, its tips at the top
  fireTile: 'textures\\effects\\FXFireScrollTileGrey.dds',    // flame that tiles, in grey
  embers: 'textures\\effects\\FXEmbers.dds',                  // a field of embers
  spark: 'textures\\effects\\FXSpark.dds',                    // one long spark
  smoke: 'textures\\effects\\FXSmokeSoftSub.dds',             // sixteen puffs of smoke, 4 by 4
  cloud: 'textures\\effects\\CloudTile.dds',                  // cloud that tiles
  frostCloud: 'textures\\effects\\CloudTileFrostSmall.dds',   // frosty cloud that tiles
  iceShards: 'textures\\effects\\IceShards01.dds',            // sixteen glints of ice, 4 by 4
  cracks: 'textures\\effects\\FXCracksTile.dds',              // cracks that tile
  bolt: 'textures\\effects\\FXLightningBolt01.dds',           // two bolts side by side, top to bottom
  swirls: 'textures\\effects\\DarkSwirls.dds',                // swirling light that tiles
  swirlStrip: 'textures\\effects\\DarkSwirlsStrip.dds'        // swirls along a strip, for streams
};

// --- Small maths ---------------------------------------------------------------------------

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const mul = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const cross = (a, b) => [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]];
const norm = a => { const l = Math.hypot(...a) || 1; return a.map(x => x / l); };
const clamp = (x, lo = 0, hi = 1) => Math.max(lo, Math.min(hi, x));

// The same scatter every build.
function random(seed) {
  let s = seed >>> 0;
  return () => { s = (s + 0x6D2B79F5) >>> 0; let t = s; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

// Value noise in three dimensions, a few octaves, 0..1.
function hash3(x, y, z, seed) {
  let h = (x * 374761393 + y * 668265263 + z * 2147483647 + seed * 974634637) | 0;
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967296;
}
function noise3(p, seed) {
  const [x, y, z] = p, xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  const f = t => t * t * (3 - 2 * t), u = f(x - xi), v = f(y - yi), w = f(z - zi);
  const l = (a, b, t) => a + (b - a) * t;
  const c = (dx, dy, dz) => hash3(xi + dx, yi + dy, zi + dz, seed);
  return l(l(l(c(0, 0, 0), c(1, 0, 0), u), l(c(0, 1, 0), c(1, 1, 0), u), v), l(l(c(0, 0, 1), c(1, 0, 1), u), l(c(0, 1, 1), c(1, 1, 1), u), v), w);
}
const fbm3 = (p, seed) => (noise3(p, seed) * 0.55 + noise3(mul(p, 2.1), seed + 1) * 0.3 + noise3(mul(p, 4.3), seed + 2) * 0.15);

// --- Geometry: plain arrays of vertices (position, normal, uv, colour) and triangles ---------

function piece() { return { verts: [], tris: [] }; }
const pack = x => Math.round((clamp(x, -1, 1) + 1) / 2 * 255);
function vertex(p, n, alpha, bright = 1, uv = [0.5, 0.5]) {
  const b = Math.round(255 * clamp(bright));
  return { pos: p, btx: 0, uv: [nifio.toHalf(uv[0]), nifio.toHalf(uv[1])], n: norm(n).map(pack), bty: 128, t: [255, 128, 128], btz: 128, c: [b, b, b, Math.round(255 * clamp(alpha))] };
}
const UP = [0, 0, 1];

// A round ball, normals outward: an octahedron split a_level times, corners shared, with a spherical
// texture wrap whose seam gets corners of its own. a_shape (optional) moves each corner out or in.
function ball(out, radius, level, { centre = [0, 0, 0], alpha = () => 1, shape = null, repeat = 2 } = {}) {
  let tris = [[[1, 0, 0], [0, 1, 0], [0, 0, 1]], [[0, 1, 0], [-1, 0, 0], [0, 0, 1]], [[-1, 0, 0], [0, -1, 0], [0, 0, 1]], [[0, -1, 0], [1, 0, 0], [0, 0, 1]],
    [[0, 1, 0], [1, 0, 0], [0, 0, -1]], [[-1, 0, 0], [0, 1, 0], [0, 0, -1]], [[0, -1, 0], [-1, 0, 0], [0, 0, -1]], [[1, 0, 0], [0, -1, 0], [0, 0, -1]]];
  for (let l = 0; l < level; l++) {
    const next = [];
    for (const [a, b, c] of tris) {
      const ab = norm(mul(add(a, b), 0.5)), bc = norm(mul(add(b, c), 0.5)), ca = norm(mul(add(c, a), 0.5));
      next.push([a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]);
    }
    tris = next;
  }
  const index = new Map();
  const corner = (d, wrap) => {
    const key = d.map(x => x.toFixed(4)).join(',') + (wrap ? 'w' : '');
    if (!index.has(key)) {
      const r = radius * (shape ? shape(d) : 1);
      let u = (Math.atan2(d[1], d[0]) / (2 * Math.PI) + 0.5) * repeat;
      if (wrap) u += repeat;
      index.set(key, out.verts.length);
      out.verts.push(vertex(add(centre, mul(d, r)), d, alpha(d), 1, [u, Math.acos(clamp(d[2], -1, 1)) / Math.PI * repeat * 0.5]));
    }
    return index.get(key);
  };
  for (const t of tris) {
    const us = t.map(d => Math.atan2(d[1], d[0]) / (2 * Math.PI) + 0.5);
    const seam = Math.max(...us) - Math.min(...us) > 0.5;
    out.tris.push(t.map((d, k) => corner(d, seam && us[k] < 0.5)));
  }
}

// A flat ring from radius a to b, its alpha given across it (several stops from inner to outer edge).
function band(out, a, b, z, alphas, segments = 128) {
  const base = out.verts.length, n = alphas.length;
  for (let s = 0; s <= segments; s++) {
    const t = (s / segments) * Math.PI * 2;
    for (let k = 0; k < n; k++) {
      const r = a + (b - a) * k / (n - 1);
      out.verts.push(vertex([Math.cos(t) * r, Math.sin(t) * r, z], UP, alphas[k], 1, [s / segments * 8, k / (n - 1)]));
    }
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < n - 1; k++) {
      const i = base + s * n + k, j = i + n;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// A disc on the ground, its alpha by distance from the middle (0..1 of its radius) and by heading.
function disc(out, radius, z, alphaAt, segments = 72, rings = 10) {
  const base = out.verts.length;
  const uv = p => [p[0] / radius * 1.4, p[1] / radius * 1.4];
  out.verts.push(vertex([0, 0, z], UP, alphaAt(0, 0), 1, [0, 0]));
  for (let k = 1; k <= rings; k++) {
    for (let s = 0; s <= segments; s++) {
      const t = (s / segments) * Math.PI * 2, r = radius * k / rings;
      const p = [Math.cos(t) * r, Math.sin(t) * r, z];
      out.verts.push(vertex(p, UP, alphaAt(k / rings, t), 1, uv(p)));
    }
  }
  const row = segments + 1, at = (k, s) => base + 1 + (k - 1) * row + s;
  for (let s = 0; s < segments; s++) out.tris.push([base, at(1, s), at(1, s + 1)]);
  for (let k = 1; k < rings; k++) {
    for (let s = 0; s < segments; s++) out.tris.push([at(k, s), at(k + 1, s), at(k, s + 1)], [at(k, s + 1), at(k + 1, s), at(k + 1, s + 1)]);
  }
}

// An upright wall round the middle, faint toward its top: frost mist rolling out.
// a_flip runs its texture's top at the wall's top (a sheet of flame rising).
function wall(out, radius, height, alphas, segments = 96, repeat = 6, flip = false) {
  const base = out.verts.length, n = alphas.length;
  for (let s = 0; s <= segments; s++) {
    const t = (s / segments) * Math.PI * 2, d = [Math.cos(t), Math.sin(t), 0];
    for (let k = 0; k < n; k++) {
      const f = k / (n - 1);
      out.verts.push(vertex([d[0] * radius, d[1] * radius, height * f], d, alphas[k], 1, [s / segments * repeat, flip ? 1 - f : f]));
    }
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < n - 1; k++) {
      const i = base + s * n + k, j = i + n;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// A half sphere over the middle, normals outward.
function dome(out, radius, { rings = 14, segments = 64, centre = [0, 0, 0], full = false, alpha = () => 1 } = {}) {
  const base = out.verts.length, top = full ? Math.PI : Math.PI / 2;
  for (let k = 0; k <= rings; k++) {
    const phi = (full ? -Math.PI / 2 : 0) + top * k / rings;
    for (let s = 0; s <= segments; s++) {
      const t = (s / segments) * Math.PI * 2;
      const d = [Math.cos(phi) * Math.cos(t), Math.cos(phi) * Math.sin(t), Math.sin(phi)];
      out.verts.push(vertex(add(centre, mul(d, radius)), d, alpha(k / rings), 1, [s / segments * 4, k / rings * 1.5]));
    }
  }
  const row = segments + 1;
  for (let k = 0; k < rings; k++) {
    for (let s = 0; s < segments; s++) {
      const i = base + k * row + s, j = i + row;
      out.tris.push([i, i + 1, j], [j, i + 1, j + 1]);
    }
  }
}

// A soft glowing line along a_points: two strips crossed at right angles, so it reads from any side,
// each brightest down its middle and gone at its edges. a_width and a_alpha take 0..1 along it. Its
// texture runs across it (0..1) and along it a texture length every vScale units, or as a_v says.
function ribbon(out, points, width, alpha, { flat = false, bright = 1, vScale = 120, v: along_v = null } = {}) {
  const n = points.length;
  let along = 0;
  const lengths = points.map((p, i) => (i ? (along += Math.hypot(...sub(p, points[i - 1]))) : 0));
  const total = along || 1;
  const sides = [];
  for (let i = 0; i < n; i++) {
    const d = norm(sub(points[Math.min(i + 1, n - 1)], points[Math.max(i - 1, 0)]));
    if (flat) { sides.push([norm(cross(d, UP))]); continue; }
    const ref = Math.abs(d[2]) < 0.9 ? UP : [1, 0, 0];
    const s1 = norm(cross(d, ref));
    sides.push([s1, norm(cross(d, s1))]);
  }
  for (let strip = 0; strip < (flat ? 1 : 2); strip++) {
    const base = out.verts.length;
    for (let i = 0; i < n; i++) {
      const f = lengths[i] / total, w = (typeof width === 'function' ? width(f) : width) / 2, a = typeof alpha === 'function' ? alpha(f) : alpha;
      const side = sides[i][strip], normal = flat ? UP : sides[i][1 - strip];
      const v = along_v ? along_v(f) : lengths[i] / vScale;
      out.verts.push(vertex(add(points[i], mul(side, -w)), normal, 0, bright, [0, v]));
      out.verts.push(vertex(points[i], normal, a, bright, [0.5, v]));
      out.verts.push(vertex(add(points[i], mul(side, w)), normal, 0, bright, [1, v]));
    }
    for (let i = 0; i < n - 1; i++) {
      const a = base + i * 3, b = a + 3;
      for (let k = 0; k < 2; k++) out.tris.push([a + k, b + k, a + k + 1], [a + k + 1, b + k, b + k + 1]);
    }
  }
}

// A body, from the feet up: [height, radius] round the middle.
const BODY = [[2, 25], [30, 27], [70, 32], [95, 31], [112, 34], [124, 30], [132, 21], [146, 20], [156, 14], [162, 0]];

// A surface turned about up through a_profile ([height, radius] from the bottom), a_scale its radius,
// its texture a_repeat times around and running down it (0 at the top) so the plugin's drift pours it
// down; alpha up it from a_alpha(f), f 0..1 from the bottom.
function turned(out, profile, { scale = 1, repeat = 4, alpha = () => 1, segments = 64, vScale = 1 } = {}) {
  const base = out.verts.length, n = profile.length, top = profile[n - 1][0], bottom = profile[0][0];
  for (let s = 0; s <= segments; s++) {
    const t = s / segments * Math.PI * 2, c = Math.cos(t), si = Math.sin(t);
    for (let k = 0; k < n; k++) {
      const [z, r] = profile[k], [z0, r0] = profile[Math.max(k - 1, 0)], [z1, r1] = profile[Math.min(k + 1, n - 1)];
      const dz = z1 - z0, dr = (r1 - r0) * scale, l = Math.hypot(dz, dr) || 1;
      const f = (z - bottom) / (top - bottom);
      out.verts.push(vertex([c * r * scale, si * r * scale, z], [c * dz / l, si * dz / l, -dr / l], alpha(f), 1, [s / segments * repeat, (1 - f) * vScale]));
    }
  }
  for (let s = 0; s < segments; s++) {
    for (let k = 0; k < n - 1; k++) {
      const i = base + s * n + k, j = i + n;
      out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
    }
  }
}

// A crystal: six sides tapering to a point, its facets alternately bright and dim. Flat-shaded.
function crystal(out, at, height, width, lean, facing, { shoulder = 0.7, alphas = [0.95, 0.85, 0.4] } = {}) {
  const sides = 6;
  const offset = frac => [Math.cos(facing) * lean * height * frac, Math.sin(facing) * lean * height * frac, height * frac];
  const ring = (frac, w) => Array.from({ length: sides }, (_, s) => {
    const t = (s / sides) * Math.PI * 2 + 0.3;
    return add(add(at, offset(frac)), [Math.cos(t) * w, Math.sin(t) * w, 0]);
  });
  const low = ring(0, width), high = ring(shoulder, width * 0.85), tip = add(at, offset(1));
  const face = (a, b, c, as, bright) => {
    const n = norm(cross(sub(b, a), sub(c, a)));
    const base = out.verts.length;
    [a, b, c].forEach((p, k) => out.verts.push(vertex(p, n, as[k], bright, [k * 0.5, p[2] / 60])));
    out.tris.push([base, base + 1, base + 2]);
  };
  for (let s = 0; s < sides; s++) {
    const n = (s + 1) % sides, bright = s % 2 ? 0.65 : 1.0;
    face(low[s], low[n], high[n], [alphas[0], alphas[0], alphas[1]], bright);
    face(low[s], high[n], high[s], [alphas[0], alphas[1], alphas[1]], bright);
    face(high[s], high[n], tip, [alphas[1], alphas[1], alphas[2]], bright * 0.9);
  }
}

// A sharp ice flake: an eight-faced diamond, taller than wide. Flat-shaded.
function flake(out, w, h) {
  const p = [[w, 0, 0], [0, w, 0], [-w, 0, 0], [0, -w, 0]], top = [0, 0, h], bottom = [0, 0, -h];
  for (let s = 0; s < 4; s++) {
    const a = p[s], b = p[(s + 1) % 4];
    for (const [x, y, z] of [[a, b, top], [b, a, bottom]]) {
      const n = norm(cross(sub(y, x), sub(z, x))), base = out.verts.length;
      out.verts.push(vertex(x, n, 1, s % 2 ? 0.7 : 1), vertex(y, n, 1, s % 2 ? 0.7 : 1), vertex(z, n, 1, 1));
      out.tris.push([base, base + 1, base + 2]);
    }
  }
}

// A square in the XY plane facing +Z, a_size across, showing one cell of an atlas a_cols across and
// a_rows down (counted along the rows from the top left): what a billboard node turns to the camera.
function sprite(out, size, { cols = 1, rows = 1, cell = 0, alpha = 1, bright = 1, centre = [0, 0, 0] } = {}) {
  const h = size / 2, col = cell % cols, row = Math.floor(cell / cols) % rows;
  const u0 = col / cols, u1 = (col + 1) / cols, v0 = row / rows, v1 = (row + 1) / rows;
  const base = out.verts.length;
  [[-h, -h, u0, v1], [h, -h, u1, v1], [h, h, u1, v0], [-h, h, u0, v0]].forEach(([x, y, u, v]) => out.verts.push(vertex(add(centre, [x, y, 0]), UP, alpha, bright, [u, v])));
  out.tris.push([base, base + 1, base + 2], [base, base + 2, base + 3]);
}

// A jagged line from a to b: a random walk off the straight path, widest in the middle, plus forks.
function jagged(rand, a, b, segments, wander) {
  const d = sub(b, a), length = Math.hypot(...d), dir = norm(d);
  const ref = Math.abs(dir[2]) < 0.9 ? UP : [1, 0, 0];
  const s1 = norm(cross(dir, ref)), s2 = norm(cross(dir, s1));
  const points = [];
  let o1 = 0, o2 = 0;
  for (let i = 0; i <= segments; i++) {
    const f = i / segments, envelope = Math.sin(Math.PI * f);
    o1 += (rand() - 0.5) * wander * 0.9; o2 += (rand() - 0.5) * wander * 0.9;
    o1 *= 0.8; o2 *= 0.8;
    const jitter = (rand() - 0.5) * wander * 0.35;
    points.push(add(add(a, mul(dir, length * f)), add(mul(s1, (o1 + jitter) * envelope * 2), mul(s2, o2 * envelope * 2))));
  }
  return points;
}

// --- The mesh writer -------------------------------------------------------------------------

function meshWriter(glow = GLOW) {
  const strings = [], blocks = [];
  const str = s => { let i = strings.indexOf(s); if (i < 0) { i = strings.length; strings.push(s); } return i; };
  const addBlock = b => blocks.push(b) - 1;
  const identity = [1, 0, 0, 0, 1, 0, 0, 0, 1];
  const desc = 8n | (4n << 8n) | (5n << 16n) | (6n << 20n) | (7n << 24n) | (0x3bn << 44n);
  // Its own shader and alpha blocks for every shape: nothing the game keeps per shape is shared.
  function shape(label, p, look) {
    if (p.verts.length > 65535) throw new Error(`${label}: too many vertices`);
    let lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (const v of p.verts) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], v.pos[k]); hi[k] = Math.max(hi[k], v.pos[k]); }
    const center = lo.map((x, k) => (x + hi[k]) / 2);
    const radius = Math.max(...p.verts.map(v => Math.hypot(...sub(v.pos, center))));
    const shader = addBlock({
      type: 'BSEffectShaderProperty', name: -1, extra: [], controller: -1,
      flags1: 0x80000000 | (look.hard ? 0 : 0x40000000) | 0x8 | (look.rim ? 0x40 : 0) | (look.palette ? 0x10 : 0),  // optional palette preserves spectral texture detail
      flags2: (look.doubleSided === false ? 0 : 0x10) | 0x20,  // closed bodies may cull their backs; vertex colours
      uvOffset: [0, 0], uvScale: look.uvScale || [1, 1], source: look.texture || glow, clamp: 3, lighting: 0, envMinLod: 0, unused: 0,
      // Dark until the plugin paints it. A glow that adds its light is faded by its colour with its alpha
      // whole (the game copies the alpha into the shader's own a frame late); dust can only fade by alpha.
      falloff: look.rim || [1, 1, 1, 1], color: look.normal ? [1, 1, 1, 0] : [0, 0, 0, 1], colorScale: look.scale, softDepth: look.soft || 30, greyscale: look.palette || ''
    });
    const alpha = addBlock({ type: 'NiAlphaProperty', name: -1, extra: [], controller: -1, alphaFlags: look.normal ? 0x10ed : 0x100d, threshold: 0 });  // added over the scene, or laid over it (dust)
    return addBlock({ type: 'BSTriShape', name: str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0], rotate: identity, scale: 1, collision: -1,
      center, radius, skin: -1, shader, alpha, desc, verts: p.verts, tris: p.tris, particleSize: 0 });
  }
  const node = (type, label, children) => addBlock({ type, name: str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0], rotate: identity, scale: 1, collision: -1, children, effects: [] });
  // A node that keeps its children square to the camera, as the game's own glows do.
  const billboard = (label, children) => addBlock({ type: 'NiBillboardNode', name: str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, 0], rotate: identity, scale: 1,
    collision: -1, children, effects: [], mode: 2 });
  function write(file, root) {
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
    for (const kind of ['shader', 'alpha']) {
      const used = sorted.filter(b => b.type === 'BSTriShape').map(b => b[kind]).filter(r => r >= 0);
      if (new Set(used).size !== used.length) throw new Error(`a ${kind} block is shared between shapes`);
    }
    const nif = { headerLine: 'Gamebryo File Format, Version 20.2.0.7\n', version: 0x14020007, endian: 1, user: 12, bsver: 100,
      author: 'Fight With Me', process: '', exportScript: '', types: [], strings, groups: [], blocks: sorted, roots: [0] };
    const bytes = nifio.write(nif);
    if (!nifio.write(nifio.read(bytes)).equals(bytes)) throw new Error('the written mesh does not read back the same');
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, bytes);
    const shapes = sorted.filter(b => b.type === 'BSTriShape');
    console.log(`wrote ${path.basename(file)}: ${bytes.length} bytes, ${sorted.length} blocks, ${shapes.reduce((t, b) => t + b.tris.length, 0)} triangles`);
  }
  return { shape, node, billboard, write, addBlock, str };
}

const soft = scale => ({ scale, rim: [1, 0, 1, 0] });                    // bright face on, gone at the edge: a glow
const edged = (scale, face = 0.15) => ({ scale, rim: [1, 0, face, 1] });  // see-through face on, bright at the edge: a shell

// A glow drawn with one of the game's effect textures.
const textured = (look, texture, uvScale = [1, 1]) => ({ ...look, texture, uvScale });

module.exports = { add, sub, mul, dot, cross, norm, clamp, random, noise3, fbm3, piece, vertex, UP, ball, band, disc, wall, dome, ribbon, BODY, turned, crystal, flake, jagged, sprite, meshWriter, GLOW, FX,
  soft, edged, textured };
