// Sunder's chest-following fracture marks and detached pieces of textured, bent armor plate.
// node tools/sunder/BuildSunder.js
const path = require('path');
const { random, piece, vertex, add, sub, mul, cross, dot, norm, ribbon, sprite, meshWriter, FX, textured } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets', 'meshes', 'FightWithMe');
const METAL = 'textures\\FightWithMe\\SunderMetal.dds';
const m = meshWriter();
const names = (prefix, count) => Array.from({ length: count }, (_, i) => `${prefix}${String(i).padStart(2, '0')}`);

// All parts start hidden, including normal-blended plate and smoke shaders. No unposed mesh can
// flash at the victim's feet during the first render frame.
function hidden(label, children) {
  return m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e,
    translate: [0, 0, 0], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: .0001, collision: -1, children, effects: [] });
}
function glow(label, size, texture, look, atlas = {}) {
  const mesh = piece(); sprite(mesh, size, atlas);
  return m.billboard(`${label}Facing`, [m.shape(label, mesh, textured(look, texture))]);
}

// A fitted shell around a torso, narrowing at its waist and neck. Paths are expressed as angle
// from the front and chest-relative height; their ribbons lie on this shell rather than floating
// in a flat rectangle in front of the actor.
function surface(angle, z, lift = 0) {
  const taper = 1 - .17 * Math.pow(Math.min(1, Math.abs(z) / 34), 1.5);
  return [Math.sin(angle) * (27 * taper + lift), Math.cos(angle) * (18 * taper + lift), z];
}
function shellNormal(p) { return norm([p[0] / (27 * 27), p[1] / (18 * 18), 0]); }

function fractureRibbon(out, path, width, strength, lift) {
  const points = path.map(([a, z]) => surface(a, z, lift)), base = out.verts.length;
  points.forEach((p, i) => {
    const along = i / (points.length - 1), tangent = norm(sub(points[Math.min(i + 1, points.length - 1)], points[Math.max(0, i - 1)]));
    const normal = shellNormal(p), side = norm(cross(tangent, normal));
    const a = strength * Math.min(1, along * 8, (1 - along) * 8), w = width * (.7 + .3 * Math.sin(along * Math.PI));
    for (let k = 0; k < 3; k++) out.verts.push(vertex(add(p, mul(side, (k - 1) * w / 2)), normal, k === 1 ? a : 0, 1, [k / 2, along]));
  });
  for (let i = 0; i + 1 < points.length; i++) for (let k = 0; k < 2; k++) {
    const a = base + i * 3 + k, b = a + 3;
    out.tris.push([a, b, a + 1], [a + 1, b, b + 1]);
  }
}

function crackPaths(grand) {
  const rand = random(grand ? 5319 : 4217);
  const paths = grand ? [
    [[-.34, 15], [-.7, 19], [-.92, 25], [-1.12, 24], [-1.45, 28]],
    [[.23, -5], [.7, -11], [1.05, -9], [1.36, -17], [1.7, -18], [2, -25]],
    [[-.08, -12], [-.6, -14], [-.94, -24], [-1.36, -22], [-1.8, -30]],
    [[1.5, 18], [1.83, 11], [2.15, 9], [2.28, 2], [2.6, -4], [2.82, -16]],
    [[-1.6, 19], [-1.9, 10], [-2.2, 6], [-2.5, -4], [-2.8, -9], [-3.1, -25]]
  ] : [
    [[-.48, 29], [-.35, 23], [-.47, 18], [-.23, 12], [-.31, 7], [-.06, 2], [.12, -5], [-.02, -12], [.24, -18], [.19, -24], [.42, -33]],
    [[-1.5, 5], [-1.16, 9], [-.86, 6], [-.56, 9], [-.31, 7], [.02, 10], [.27, 6], [.6, 10], [.94, 5], [1.3, 7], [1.58, 2]],
    [[-1.3, -25], [-.93, -19], [-.67, -20], [-.35, -14], [-.02, -12]],
    [[.12, -5], [.42, -4], [.65, -10], [.96, -12], [1.1, -19]],
    [[3.0, 24], [2.85, 15], [3.02, 10], [2.87, 4], [3.1, -4], [2.91, -11], [3.12, -22]]
  ];
  const branches = [];
  paths.forEach((main, index) => {
    for (let j = 1; j + 1 < main.length; j += 2) {
      const [angle, z] = main[j], dir = (j + index) % 2 ? -1 : 1;
      const length = .18 + .32 * rand(), height = (rand() - .5) * 17;
      branches.push([[angle, z], [angle + length * .46 * dir, z + height * .3],
        [angle + length * .63 * dir, Math.max(-33, Math.min(29, z + height * .72))],
        [angle + length * dir, Math.max(-34, Math.min(30, z + height))]]);
    }
  });
  return { mains: paths, branches };
}
function cracks(label, grand) {
  const core = piece(), haze = piece(), paths = crackPaths(grand);
  for (const path of paths.mains) {
    fractureRibbon(core, path, grand ? 1.2 : 1.0, 1, .2);
    fractureRibbon(haze, path, grand ? 3.3 : 2.8, .19, .12);
  }
  for (const path of paths.branches) {
    fractureRibbon(core, path, .64, .84, .2);
    fractureRibbon(haze, path, 1.9, .12, .12);
  }
  return hidden(label, [m.shape(`${label}FineFractures`, core, { scale: 1.25, hard: true }),
    m.shape(`${label}FractureGlow`, haze, { scale: 1, hard: true })]);
}

// Each chip is a closed, slightly bent plate with torn edges. Broad textured faces and very thin
// sidewalls make it read as armor instead of a pointed crystal or a piece of rock. Every fragment
// has its own shape/shader so fading one cannot change another live hit.
function armorChip(seed) {
  const rand = random(seed), out = piece(), width = 3.8 + 3.6 * rand(), height = 8 + 5 * rand();
  const thickness = .25 + .2 * rand(), bend = .5 + .75 * rand(), twist = (rand() - .5) * .55;
  const uvBase = [rand() * .48, rand() * .48], uvScale = .3 + .2 * rand();
  // Long cut edges and clipped corners, not a ring of equally spaced radial vertices. These
  // silhouettes read as pieces sheared from a larger breastplate, including a few narrow tears.
  const outlines = [
    [[-1, -.8], [.57, -1], [1, -.68], [.72, .78], [.14, 1], [-.9, .58]],
    [[-.83, -1], [.66, -.83], [.94, .36], [.35, .91], [-.73, .67]],
    [[-1, -.8], [.38, -.98], [.85, -.1], [.63, .98], [-.3, .7]],
    [[-.54, -1], [.78, -.78], [.43, .8], [-.08, 1], [-.71, .26]],
    [[-.92, -.93], [.55, -.76], [1, -.15], [.76, .61], [-.42, .98], [-.8, .56]],
    [[-.8, -.95], [.9, -.72], [.72, .66], [.25, .88], [-1, .32]]
  ];
  const outline = outlines[seed % outlines.length].map(([x, z]) => [x * (1 + .08 * (rand() - .5)), z * (1 + .08 * (rand() - .5))]);
  const frontY = (x, z) => .25 - bend * x * x / (width * width) + twist * x * z / (width * height);
  const frontNormal = (x, z) => norm([2 * bend * x / (width * width) - twist * z / (width * height), 1, -twist * x / (width * height)]);
  const front = outline.map(([x, z]) => [x * width, frontY(x * width, z * height) - .1, z * height]);
  const inner = outline.map(([x, z]) => [x * width * .94, frontY(x * width * .94, z * height * .96), z * height * .96]);
  const back = front.map(([x, y, z]) => [x, y - thickness, z]);
  const centerF = [0, .25, 0], centerB = [0, .15 - thickness, 0];
  function face(a, b, c, outward, bright, smooth = 0) {
    let n = norm(cross(sub(b, a), sub(c, a)));
    if (dot(n, outward) < 0) { [b, c] = [c, b]; n = mul(n, -1); }
    const base = out.verts.length;
    for (const p of [a, b, c]) {
      // Smooth the broad bent sheet; the torn sidewalls and narrow bevel retain hard normals.
      const normal = smooth ? mul(frontNormal(p[0], p[2]), smooth) : n;
      const shade = bright * (.74 + .26 * Math.max(0, dot(normal, norm([.3, 1, .7]))));
      out.verts.push(vertex(p, normal, 1, shade,
        [uvBase[0] + (p[0] / width + 1) * .5 * uvScale, uvBase[1] + (1 - p[2] / height) * .5 * uvScale]));
    }
    out.tris.push([base, base + 1, base + 2]);
  }
  const edges = front.length;
  for (let i = 0; i < edges; i++) {
    const j = (i + 1) % edges;
    face(centerF, inner[i], inner[j], [0, 1, 0], .9, 1);
    face(inner[i], front[i], front[j], [0, 1, 0], 1);
    face(inner[i], front[j], inner[j], [0, 1, 0], 1);
    face(centerB, back[j], back[i], [0, -1, 0], .79, -1);
    const outward = norm([front[i][0] + front[j][0], 0, front[i][2] + front[j][2]]);
    face(front[i], back[i], back[j], outward, .54);
    face(front[i], back[j], front[j], outward, .54);
  }
  return out;
}

const mark = m.node('NiNode', 'Mark', [cracks('Cracks', false), cracks('GrandCracks', true)]);
const burst = [];
// Localized brief contact glint, never a large spherical explosion that hides the armor.
burst.push(hidden('Flash', [glow('ImpactGlint', 37, FX.glowSpot, { scale: .72 })]));
const dustPuffs = [];
for (let i = 0; i < 4; i++) dustPuffs.push(glow(`MetalDust${i}`, 36 + i * 5, FX.smoke,
  { scale: .65, normal: true, soft: 16 }, { cols: 4, rows: 4, cell: i * 3 + 1, alpha: .65, centre: [(i % 2 ? -1 : 1) * 9, 0, -6 + i * 4] }));
burst.push(hidden('Dust', dustPuffs));
names('Fragment', 28).forEach((name, i) => burst.push(hidden(name,
  [m.shape(`${name}Plate`, armorChip(8300 + i), textured({ scale: 1.05, normal: true, doubleSided: false, hard: true }, METAL))])));
names('Spark', 16).forEach(name => {
  const streak = piece();
  ribbon(streak, [[-17, 0, 0], [-9, 0, 0], [-2, 0, 0], [0, 0, 0]], f => .5 + 1.2 * f, f => f);
  burst.push(hidden(name, [m.shape(`${name}Streak`, streak, { scale: 1.1 }), glow(`${name}Glint`, 5, FX.glowSpot, { scale: .7 })]));
});
const root = m.node('BSFadeNode', 'Sunder', [mark, m.node('NiNode', 'Burst', burst)]);
for (const suffix of ['', '2', '3', '4']) m.write(path.join(OUT, `SunderBreak${suffix}.nif`), root);
