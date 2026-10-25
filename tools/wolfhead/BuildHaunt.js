// Builds meshes\FightWithMe\Haunt.nif, the ghostly smoke that holds whoever Kynareth's Maw's bite
// catches: for each of six caught at once, a dark mist laid over them, shaped to a body and drifting up
// it, a ghost's glow round its edges, a fog at their feet, a burst of light as they are caught, and wisps
// of smoke curling up off them. The mist and the fog are the game's own cloud, laid over the scene
// rather than glowing, so the smoke reads dark; the rest glows. Everything starts unseen: the plugin
// poses every part and sets its colour and strength each frame (src/Haunt.cpp holds the numbers).
//
//   node tools\wolfhead\BuildHaunt.js

const path = require('path');
const { piece, band, disc, sprite, meshWriter, FX, textured, edged, BODY, turned } = require('../fx/meshkit.js');

const OUT = path.join(__dirname, '..', '..', 'assets');
const SLOTS = 6;   // Haunt::kSlots
const WISPS = 8;   // Haunt::kWisps

// How strongly each piece shows (its shader's colour scale): the mist and fog lay over the scene, the
// glow, the flare and the wisps add their light, the game's soft textures running hot.
const LOOK = { mist: 1.0, glow: 1.8, fog: 1.0, ring: 0.9, flare: 1.6, wisp: 2.0 };

function glowSprite(m, label, size, look, texture, atlas = {}) {
  const quad = piece();
  sprite(quad, size, atlas);
  return m.billboard(`${label}Turn`, [m.shape(label, quad, textured(look, texture))]);
}

function at(m, label, z, children) {
  return m.addBlock({ type: 'NiNode', name: m.str(label), extra: [], controller: -1, flags: 0x8000e, translate: [0, 0, z], rotate: [1, 0, 0, 0, 1, 0, 0, 0, 1], scale: 1,
    collision: -1, children, effects: [] });
}

function build() {
  const m = meshWriter(), parts = [];
  for (let k = 0; k < SLOTS; k++) {
    const name = `Haunt${k}`;
    // The dark mist: thick at their edges, thin face on so they show through, gone at their feet and crown.
    const mist = piece();
    turned(mist, BODY, { scale: 1.08, repeat: 3, vScale: 1.6, alpha: f => Math.min(1, f / 0.1) * Math.min(1, (1 - f) / 0.2) });
    // The ghost's glow round it, a little further out.
    const glow = piece();
    turned(glow, BODY, { scale: 1.22, repeat: 2, vScale: 1.2, alpha: f => Math.min(1, f / 0.08) * Math.min(1, (1 - f) / 0.25) * 0.9 });
    // Fog pooled at their feet, and a faint ring of the glow round it.
    const fog = piece();
    disc(fog, 72, 2, r => (r < 1 ? 0.8 * (1 - Math.pow(r, 2.5)) : 0), 40, 8);
    const ring = piece();
    band(ring, 40, 56, 2.4, [0, 0.7, 0], 48);
    const wisps = [];
    for (let w = 0; w < WISPS; w++) {
      wisps.push(m.node('NiNode', `${name}Wisp${w}`, [glowSprite(m, `${name}Wisp${w}Puff`, 58, { scale: LOOK.wisp }, FX.smoke, { cols: 4, rows: 4, cell: (k * 3 + w * 5) % 16 })]));
    }
    parts.push(m.node('NiNode', name, [
      m.node('NiNode', `${name}Mist`, [m.shape(`${name}MistSmoke`, mist, textured({ ...edged(LOOK.mist, 0.25), normal: true }, FX.cloud, [1, 1]))]),
      m.node('NiNode', `${name}Glow`, [m.shape(`${name}GlowLight`, glow, textured(edged(LOOK.glow, 0.05), FX.cloud, [1, 1]))]),
      m.node('NiNode', `${name}Ground`, [m.shape(`${name}GroundFog`, fog, textured({ scale: LOOK.fog, normal: true }, FX.cloud, [1.5, 1.5]))]),
      m.node('NiNode', `${name}Ring`, [m.shape(`${name}RingGlow`, ring, { scale: LOOK.ring })]),
      m.node('NiNode', `${name}Flare`, [at(m, `${name}FlareLift`, 78, [glowSprite(m, `${name}FlareLight`, 170, { scale: LOOK.flare }, FX.glowSoft)])]),
      ...wisps
    ]));
  }
  // Written twice, the second as Haunt2.nif: the game gives no body to a second copy of a mesh while
  // one still plays, so a second wolf's catch takes the other name.
  const root = m.node('BSFadeNode', 'Haunt', parts);
  for (const file of ['Haunt.nif', 'Haunt2.nif']) m.write(path.join(OUT, 'meshes', 'FightWithMe', file), root);
}

build();
