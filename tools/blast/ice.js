// Fractured glacier ice for Elemental Burst. Broad, irregular plates instead of tapered crystal
// points: the ledges and broken crown keep a readable silhouette even while the texture is fading.
const { vertex, random, cross, sub, norm, dot } = require('../fx/meshkit.js');

function iceSlab(out, at, height, width, { seed, depth = 0.4, lean = 0, facing = 0, turn = 0, crownSlope = null } = {}) {
  const rand = random(seed), cos = Math.cos(turn), sin = Math.sin(turn);
  // A bevelled plate: deliberately unequal corners, so this never reads as a regular hexagon.
  const outline = [[-1, -.56], [-.71, -1], [.64, -1], [1, -.51], [.93, .55], [.61, 1], [-.67, .92], [-1, .48]];
  const rim = outline.map(([x, y]) => [x * (.9 + .16 * rand()), y * (.9 + .16 * rand())]);
  const slant = crownSlope === null ? (rand() < .5 ? -1 : 1) * (.16 + .1 * rand()) : crownSlope;
  const crown = rim.map(([x]) => .80 + .08 * rand() + x * slant);
  const uOffset = rand() * .7, vOffset = rand() * .7;
  const bend = (rand() - .5) * width * .2;
  const place = ([x, y, z]) => [at[0] + cos * x - sin * y + Math.cos(facing) * lean * z,
    at[1] + sin * x + cos * y + Math.sin(facing) * lean * z, at[2] + z];
  // The two upper rings have unequal heights. Large planes meet chipped ledges, rather than
  // continuing into a mechanically identical pointed cap on every slab.
  const rings = [
    rim.map(([x, y]) => [x * width * .84, y * width * depth * .9, 0]),
    rim.map(([x, y], i) => [x * width, y * width * depth, height * (.30 + .05 * rand())]),
    rim.map(([x, y], i) => [x * width * (.82 + .08 * rand()) + bend, y * width * depth * .86, height * Math.min(crown[i] - .13, .68 + .08 * rand())]),
    rim.map(([x, y], i) => [x * width * (.44 + .16 * rand()) + bend * 1.5, y * width * depth * (.57 + .15 * rand()), height * crown[i]])
  ];

  function face(local, bright = 1, top = false) {
    const points = local.map(place);
    const summedNormal = [0, 0, 0];
    for (let i = 1; i + 1 < local.length; i++) {
      const n = norm(cross(sub(local[i], local[0]), sub(local[i + 1], local[0])));
      for (let k = 0; k < 3; k++) summedNormal[k] += n[k];
    }
    const localNormal = norm(summedNormal);
    // Project the whole face at a stable physical scale. Both triangles share these coordinates;
    // there is no restart/stretch at the diagonal, and the cloudy cracks follow the solid ice.
    const horizontal = top || Math.abs(localNormal[2]) > Math.max(Math.abs(localNormal[0]), Math.abs(localNormal[1]));
    const side = Math.abs(localNormal[0]) > Math.abs(localNormal[1]);
    const reference = horizontal || !side ? [1, 0, 0] : [0, 1, 0];
    const tangent = norm(reference.map((value, i) => value - localNormal[i] * dot(reference, localNormal)));
    let bitangent = norm(cross(localNormal, tangent));
    if (!horizontal && bitangent[2] < 0) bitangent = bitangent.map(value => -value);
    const texelScale = 1 / 105;
    for (let i = 1; i + 1 < points.length; i++) {
      const indices = [0, i, i + 1], n = norm(cross(sub(points[i], points[0]), sub(points[i + 1], points[0])));
      const faceLight = (.80 + .18 * Math.max(0, n[0] * -.35 + n[1] * -.45 + n[2] * .8)) * bright;
      const base = out.verts.length;
      for (const index of indices) {
        const alpha = .48 + .08 * Math.min(1, local[index][2] / Math.max(1, height * .25));
        out.verts.push(vertex(points[index], n, alpha, faceLight, [uOffset + dot(local[index], tangent) * texelScale, vOffset + dot(local[index], bitangent) * texelScale]));
      }
      out.tris.push([base, base + 1, base + 2]);
    }
  }

  for (let row = 0; row < rings.length - 1; row++) {
    for (let i = 0; i < rim.length; i++) {
      const j = (i + 1) % rim.length;
      face([rings[row][i], rings[row][j], rings[row + 1][j], rings[row + 1][i]], .93 + .07 * rand());
    }
  }
  // Close both ends: flying fragments remain solid when they tumble. The broken upper surface
  // is a shallow, sloping fracture plane with uneven corners, never a pyramid tip.
  const top = rings[rings.length - 1], centre = top.reduce((p, v) => p.map((x, k) => x + v[k] / top.length), [0, 0, 0]);
  for (let i = 0; i < rim.length; i++) face([top[i], top[(i + 1) % rim.length], centre], 1.07, true);
  face([...rings[0]].reverse(), .8, true);
}

module.exports = { iceSlab };
