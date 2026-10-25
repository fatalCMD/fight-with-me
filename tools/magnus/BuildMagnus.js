// Builds meshes\FightWithMe\MagnusOrb.nif, the great orb that floats behind a mage while Magnus holds,
// entirely from code: a white-hot core inside two skins of drifting light turning against each other, a
// wide soft halo, three rings of light tipped across it like an armillary's, three streams of magicka
// pouring out of it, and the motes that run down them; for the grand orb of Magnus's third rank, wider
// veils of magicka and comets spiralling down them. Everything glows with the game's own effect textures:
// soft glow sprites the game keeps turned to the camera for the heart, the halo and the motes, its
// swirling light on the skins, its swirl strip down the streams. Every shader starts unseen, and the plugin gives each its colour and strength, poses
// the orb behind the mage, and points the streams and motes at them (src/MagnusOrb.cpp has the numbers).
//
//   node tools\magnus\BuildMagnus.js

const path = require('path');
const { mul, fbm3, piece, vertex, UP, ball, ribbon, sprite, meshWriter, FX, edged, textured } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const RADIUS = 72;           // MagnusOrb::kRadius
const TENDRIL = 100;         // MagnusOrb::kTendrilLength
const TENDRILS = 3;          // MagnusOrb::kTendrils
const FLOWS = 24;            // MagnusOrb::kFlows
const WISPS = 12;            // MagnusOrb::kWisps

// How brightly each piece glows (its shader's colour scale).
const LOOK = { core: 0.9, hot: 1.2, shell: 0.55, swirl: 0.4, halo: 0.35, ring: 0.8, stream: 1.1, flow: 1.4, veil: 1.0, wisp: 1.3, rim: 0.45 };

// A sprite of a game texture, kept square to the camera.
function glowSprite(label, size, scale, texture) {
  const quad = piece();
  sprite(quad, size);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured({ scale }, texture))]);
}

// A ring of light in its own flat plane, broken into dashes, with a fine whole line inside it.
function dashedRing(out, radius, width, dashes, fill) {
  const arc = (from, to, r, w, alpha) => {
    const steps = Math.max(2, Math.ceil((to - from) / (Math.PI / 48))), base = out.verts.length;
    for (let s = 0; s <= steps; s++) {
      const t = from + (to - from) * s / steps;
      for (const [dr, a] of [[-w / 2, 0], [0, alpha], [w / 2, 0]]) out.verts.push(vertex([Math.cos(t) * (r + dr), Math.sin(t) * (r + dr), 0], UP, a));
    }
    for (let s = 0; s < steps; s++) {
      for (let k = 0; k < 2; k++) {
        const i = base + s * 3 + k, j = i + 3;
        out.tris.push([i, j, i + 1], [i + 1, j, j + 1]);
      }
    }
  };
  for (let d = 0; d < dashes; d++) {
    const from = (d / dashes) * Math.PI * 2;
    arc(from, from + (Math.PI * 2 / dashes) * fill, radius, width, 1);
  }
  arc(0, Math.PI * 2, radius - width * 1.6, width * 0.45, 0.45);
}

const m = meshWriter();
const orbParts = [];
orbParts.push(m.node('NiNode', 'Core', [glowSprite('CoreGlow', 90, LOOK.core, FX.glowSpot), glowSprite('CoreHot', 34, LOOK.hot, FX.glowSoft)]));
{
  // The outer skin: drifting light seen most at its rim, a little uneven, as if it were alive.
  const skin = piece();
  ball(skin, RADIUS, 5, { shape: d => 0.97 + 0.06 * fbm3(mul(d, 2.0), 311), repeat: 2 });
  orbParts.push(m.node('NiNode', 'Shell', [m.shape('ShellSkin', skin, textured(edged(LOOK.shell, 0.12), FX.swirls, [2, 1]))]));
  const inner = piece();
  ball(inner, RADIUS * 0.75, 5, { shape: d => 0.95 + 0.1 * fbm3(mul(d, 2.6), 377), repeat: 3 });
  orbParts.push(m.node('NiNode', 'Swirl', [m.shape('SwirlSkin', inner, textured(edged(LOOK.swirl, 0.25), FX.swirls, [1, 1]))]));
  orbParts.push(m.node('NiNode', 'Halo', [glowSprite('HaloGlow', RADIUS * 4.2, LOOK.halo, FX.glowSpot)]));
  // Grand only: several fine, textured shells dissolve into a corona, with no hard blue
  // polygonal silhouette. Independent radii and noise give the edge depth as it drifts.
  const corona = [];
  for (let layer = 0; layer < 3; ++layer) {
    const mist = piece();
    ball(mist, RADIUS * (1.12 + layer * 0.12), 5, {
      shape: d => 0.98 + 0.04 * fbm3(mul(d, 3.4), 500 + layer),
      alpha: d => 0.35 + 0.55 * fbm3(mul(d, 4.8), 710 + layer), repeat: 3
    });
    corona.push(m.shape(`CoronaMist${layer}`, mist, textured(edged(0.36 - layer * 0.08, 0.025), FX.frostCloud, [1,1])));
  }
  const flares = piece();
  for (let k=0;k<14;++k) {
    const points = Array.from({length:80},(_,i)=>{
      const f=i/79, a=k*2.39996+f*1.25, tilt=(k%5-2)*0.32;
      const r=RADIUS*(1.10+0.16*Math.sin(Math.PI*f));
      return [r*Math.cos(a),r*Math.sin(a)*Math.cos(tilt),r*Math.sin(a)*Math.sin(tilt)];
    });
    ribbon(flares,points,f=>1.0+3.0*Math.sin(Math.PI*f),f=>Math.pow(Math.sin(Math.PI*f),1.6)*0.48,{vScale:65});
  }
  corona.push(m.shape('CoronaFilaments',flares,textured({scale:0.6,soft:10},FX.swirlStrip)));
  orbParts.push(m.node('NiNode','Rim',corona));
}
[[RADIUS * 1.25, 3.2, 14, 0.62], [RADIUS * 1.38, 2.6, 22, 0.5], [RADIUS * 1.5, 2.2, 9, 0.7]].forEach(([r, w, dashes, fill], i) => {
  const ring = piece();
  dashedRing(ring, r, w, dashes, fill);
  orbParts.push(m.node('NiNode', `Ring${i}`, [m.shape(`Ring${i}Light`, ring, { scale: LOOK.ring })]));
});
const parts = [m.node('NiNode', 'Orb', orbParts)];

// The streams: a wavering ribbon along +X from the orb to the mage, thinning as it goes, swirls pouring
// down it (one texture length every sixty units).
for (let k = 0; k < TENDRILS; k++) {
  const stream = piece();
  const points = Array.from({ length: 24 }, (_, i) => {
    const f = i / 23;
    return [TENDRIL * f, 4 * Math.sin(f * Math.PI * 3 + k * 2) * Math.sin(Math.PI * f), 3 * Math.cos(f * Math.PI * 2 + k) * Math.sin(Math.PI * f)];
  });
  ribbon(stream, points, f => 9 * (1 - 0.6 * f) + 1.5, f => 0.9 * Math.min(1, f * 6) * (1 - 0.35 * f), { vScale: 60 });
  parts.push(m.node('NiNode', `Tendril${k}`, [m.shape(`Tendril${k}Stream`, stream, textured({ scale: LOOK.stream }, FX.swirlStrip))]));
}
// The grand orb's veils: wider streams, waving more, the same way along +X.
for (let k = 0; k < TENDRILS; k++) {
  const veil = piece();
  const points = Array.from({ length: 28 }, (_, i) => {
    const f = i / 27;
    return [TENDRIL * f, 9 * Math.sin(f * Math.PI * 4 + k * 2) * Math.sin(Math.PI * f), 7 * Math.cos(f * Math.PI * 3 + k) * Math.sin(Math.PI * f)];
  });
  ribbon(veil, points, f => 22 * (1 - 0.6 * f) + 3, f => 0.85 * Math.min(1, f * 5) * (1 - 0.3 * f), { vScale: 50 });
  parts.push(m.node('NiNode', `Veil${k}`, [m.shape(`Veil${k}Stream`, veil, textured({ scale: LOOK.veil }, FX.swirlStrip))]));
}
// And its wisps: comets of magicka, a bright head at the node and a tail back along -X.
for (let i = 0; i < WISPS; i++) {
  const tail = piece();
  ribbon(tail, Array.from({ length: 8 }, (_, j) => [-48 + j / 7 * 48, 0, 0]), f => 1 + 8 * f, f => f * f);
  const name = `Wisp${String(i).padStart(2, '0')}`;
  parts.push(m.node('NiNode', name, [m.shape(`${name}Tail`, tail, { scale: LOOK.wisp }), glowSprite(`${name}Head`, 22, LOOK.wisp, FX.glowSpot)]));
}
for (let i = 0; i < FLOWS; i++) {
  parts.push(m.node('NiNode', `Flow${String(i).padStart(2, '0')}`, [glowSprite(`Flow${String(i).padStart(2, '0')}Glow`, 16, LOOK.flow, FX.glowSpot)]));
}
m.write(path.join(OUT, 'meshes', 'FightWithMe', 'MagnusOrb.nif'), m.node('BSFadeNode', 'MagnusOrb', parts));
