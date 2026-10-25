// Fight With Me's abilities with models, as the game draws them: Kynareth's Maw's bite and the
// healer's circle. The meshes the plugin ships, the textures the game would load, and the frames the
// plugin's own code produces (out/*.json, from build.js). Lighting is an approximation: no ENB, no
// game shaders.
import * as THREE from 'three';
import { SoundPlayer } from './sound-player.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const $ = id => document.getElementById(id);
const status = $('status');
// Nodes the game keeps turned to the camera, and the textures the plugin drifts (Posing::Noisy's list).
const billboards = [];
const drifts = source => /(fxfirescrolltilegrey|cloudtile|cloudtilefrostsmall|darkswirls|darkswirlsstrip|fxembers|healingpulse)\.dds$/i.test(source || '');

const files = ['out/head.json', 'out/head-plain.json', 'out/pose.json', 'out/circle.json', 'out/circle-look.json', 'out/blast-fire.json', 'out/blast-frost.json', 'out/blast-shock.json', 'out/blast-look.json', 'out/magnus.json',
  'out/magnus-look.json', 'out/quake.json', 'out/quake-look.json', 'out/rain.json', 'out/rain-look.json', 'out/cloak.json', 'out/cloak-look.json', 'out/bear.json', 'out/bear-plain.json', 'out/bear-look.json', 'out/head-red.json', 'out/head-red-plain.json', 'out/haunt.json', 'out/chaos.json', 'out/chaos-look.json', 'out/chaos-grand.json', 'out/rejuvenation.json', 'out/rejuvenation-look.json', 'out/bear-red.json', 'out/bear-red-plain.json', 'out/sunder.json', 'out/sunder-look.json', 'out/chaos-binding.json', 'out/weakwill.json', 'out/weakwill-grand.json', 'out/weakwill-look.json', 'out/bastion.json', 'out/bastion-grand.json', 'out/bastion-look.json', 'out/warcry-sabre.json', 'out/warcry-sabre-red.json'];
const [furred, plain, pose, circleMesh, circleLook, fireMesh, frostMesh, shockMesh, blastLook, magnusMesh, magnusLook, quakeMesh, quakeLook, rainMesh, rainLook, cloakMesh, cloakLook, bearMesh, bearPlainMesh, bearLook, redFurred, redPlain, hauntMesh, chaosMesh, chaosLook, chaosGrandMesh, rejuvenationMesh, rejuvenationLook, bearRedMesh, bearRedPlainMesh, sunderMesh, sunderLook, chaosBindingMesh, weakWillMesh, weakWillGrandMesh, weakWillLook, bastionMesh, bastionGrandMesh, bastionLook, warSabreMesh, warDragonMesh] = await Promise.all(files.map(f => fetch(f).then(r => {
  if (!r.ok) throw new Error(`${f} is missing: run node tools\\wolfhead\\preview\\build.js`);
  return r.json();
}))).catch(e => { status.textContent = e.message; throw e; });

// --- Scene: the game's Z-up world inside three's Y-up one ---------------------------------------

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
$('view').appendChild(renderer.domElement);

const scene = new THREE.Scene();
const world = new THREE.Group();
world.rotation.x = -Math.PI / 2;  // game (x, y, z) -> three (x, z, -y)
scene.add(world);
const toThree = (x, y, z) => new THREE.Vector3(x, z, -y);

const camera = new THREE.PerspectiveCamera(65, 1, 1, 20000);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

const ground = new THREE.Mesh(new THREE.PlaneGeometry(6000, 6000), new THREE.MeshStandardMaterial({ color: 0x2c2a26, roughness: 1 }));
ground.receiveShadow = true;
world.add(ground);
const grid = new THREE.GridHelper(3000, 30, 0x555048, 0x3a3732);
grid.position.y = 0.5;
scene.add(grid);

// --- Lighting presets ------------------------------------------------------------------------

const lights = new THREE.Group();
world.add(lights);
function setLight(kind) {
  lights.clear();
  if (kind === 'day') {
    scene.background = new THREE.Color(0x8fa6bd);
    lights.add(new THREE.HemisphereLight(0xcfe0ff, 0x4a4034, 1.1));
    const sun = new THREE.DirectionalLight(0xfff0d8, 2.4);
    sun.position.set(-600, -500, 900);
    sun.castShadow = true;
    sun.shadow.mapSize.set(2048, 2048);
    Object.assign(sun.shadow.camera, { left: -500, right: 500, top: 500, bottom: -500, near: 10, far: 3000 });
    lights.add(sun);
  } else if (kind === 'dungeon') {
    scene.background = new THREE.Color(0x0d0c0b);
    lights.add(new THREE.AmbientLight(0x404858, 0.35));
    const torch = new THREE.PointLight(0xffa860, 60000, 1400, 2);
    torch.position.set(-180, -260, 220);
    torch.castShadow = true;
    lights.add(torch);
  } else {
    scene.background = new THREE.Color(0x0a0f1a);
    lights.add(new THREE.HemisphereLight(0x5a6c99, 0x101014, 0.5));
    const moon = new THREE.DirectionalLight(0xa8bce8, 0.9);
    moon.position.set(500, -300, 1000);
    moon.castShadow = true;
    lights.add(moon);
  }
}

// --- The heads -------------------------------------------------------------------------------

const loader = new THREE.TextureLoader();
const textures = new Map();
function texture(file, srgb) {
  if (!file) return null;
  const key = `${file}|${srgb}`;
  if (!textures.has(key)) {
    const t = loader.load(`out/${file}`);
    t.flipY = false;  // the game's UVs start at the top
    t.colorSpace = srgb ? THREE.SRGBColorSpace : THREE.NoColorSpace;
    t.anisotropy = renderer.capabilities.getMaxAnisotropy();
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    textures.set(key, t);
  }
  return textures.get(key);
}

// The game's unlit glow shader (BSEffectShaderProperty) as Skyrim draws it: the source texture's
// green channel read through the palette, replacing RGB rather than multiplying the tint;
// BaseColor.r and vertex red choose the palette row. Alpha comes from the texture,
// the vertices, the fade and the view-angle falloff; tested, then added over the scene. Skyrim adds
// the texture's values as they are, never treating them as linear light, so the result is written
// straight out too: brightening it for the screen made dim glows look fine here and vanish in game.
const clamped = t => { t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping; return t; };
function glowMaterial(s) {
  const e = s.effect;
  const falloff = e.falloff || [1, 1, 1, 1];
  return new THREE.ShaderMaterial({
    uniforms: {
      map: { value: texture(s.diffuse, false) },
      palette: { value: e.palette ? clamped(texture(e.palette, true)) : null },
      usePalette: { value: e.palette ? 1 : 0 },
      tint: { value: new THREE.Vector4(...e.color) },
      scale: { value: e.scale },
      falloff: { value: new THREE.Vector4(...falloff) },
      useFalloff: { value: e.falloff ? 1 : 0 },
      threshold: { value: s.threshold / 255 },
      opacity: { value: 1 },
      uvScale: { value: new THREE.Vector2(...(e.uvScale || [1, 1])) },
      uvOffset: { value: new THREE.Vector2(...(e.uvOffset || [0, 0])) }
    },
    vertexShader: `
      attribute vec4 color;
      uniform vec2 uvScale; uniform vec2 uvOffset;
      varying vec2 vUv; varying vec4 vColor; varying vec3 vNormal; varying vec3 vView;
      void main() {
        vUv = uv * uvScale + uvOffset; vColor = color;
        vec4 view = modelViewMatrix * vec4(position, 1.0);
        vNormal = normalize(normalMatrix * normal);
        vView = normalize(-view.xyz);
        gl_Position = projectionMatrix * view;
      }`,
    fragmentShader: `
      uniform sampler2D map; uniform sampler2D palette; uniform int usePalette;
      uniform vec4 tint; uniform float scale; uniform vec4 falloff; uniform int useFalloff;
      uniform float threshold; uniform float opacity;
      varying vec2 vUv; varying vec4 vColor; varying vec3 vNormal; varying vec3 vView;
      void main() {
        vec4 source = texture2D(map, vUv);
        vec3 rgb = usePalette == 1
          ? texture2D(palette, vec2(source.g, clamp(tint.r * vColor.r, 0.0, 1.0))).rgb * scale
          : source.rgb * tint.rgb * scale * vColor.rgb;
        float a = source.a * tint.a * vColor.a * opacity;
        if (useFalloff == 1) {
          float facing = abs(dot(normalize(vNormal), normalize(vView)));
          float t = clamp((falloff.x - facing) / max(falloff.x - falloff.y, 0.0001), 0.0, 1.0);
          a *= mix(falloff.z, falloff.w, t);
        }
        if (threshold > 0.0 && a <= threshold) discard;
        gl_FragColor = vec4(rgb, a);
      }`,
    side: s.doubleSided ? THREE.DoubleSide : THREE.FrontSide,
    transparent: true,
    depthWrite: false,
    blending: s.additive ? THREE.CustomBlending : THREE.NormalBlending,
    blendSrc: THREE.SrcAlphaFactor,
    blendDst: THREE.OneFactor
  });
}

function geometryOf(s) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(s.positions, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(s.normals, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(s.uvs, 2));
  g.setAttribute('color', new THREE.Float32BufferAttribute(s.colors, 4));
  g.setIndex(s.indices);
  return g;
}

function buildHead(data) {
  const head = new THREE.Group();
  head.matrixAutoUpdate = false;
  const jaw = new THREE.Group();
  jaw.position.set(...data.jaw);
  head.add(jaw);
  const materials = [];
  for (const s of data.shapes) {
    const g = geometryOf(s);
    // The game's alpha test: drawn where texture alpha x vertex alpha x fade passes the threshold.
    const m = s.effect ? glowMaterial(s) : new THREE.MeshStandardMaterial({
      map: texture(s.diffuse, true), normalMap: texture(s.normal, false), normalScale: new THREE.Vector2(1, -1),
      vertexColors: true, alphaTest: Math.max(s.threshold / 255, 0.004), side: s.doubleSided ? THREE.DoubleSide : THREE.FrontSide,
      roughness: 0.78, metalness: 0
    });
    materials.push(m);
    const mesh = new THREE.Mesh(g, m);
    mesh.castShadow = true;
    (s.node === 'Jaw' ? jaw : head).add(mesh);
  }
  world.add(head);
  return { head, jaw, materials };
}
const heads = { furred: buildHead(furred), plain: buildHead(plain), redFurred: buildHead(redFurred), redPlain: buildHead(redPlain) };

// The healer's circle: its node tree as the mesh has it, so the runes and the two aura lines move alone.
function buildCircle(data) {
  const groups = new Map(), top = new THREE.Group();
  for (const n of data.nodes) { const g = new THREE.Group(); g.position.set(...n.translate); groups.set(n.name, g); if (n.billboard) billboards.push(g); }
  for (const n of data.nodes) (n.parent ? groups.get(n.parent) : top).add(groups.get(n.name));
  const part = name => { const at = data.nodes.find(x => x.name === name); return at && at.billboard ? at.parent : name; };  // a sprite's own part, past its billboard
  const glowing = [], aura = [[], []], flowing = [], inner = [], heart = [], moteMaterials = new Map(), glintMaterials = new Map();
  for (const s of data.shapes) {
    const m = glowMaterial(s);
    groups.get(s.node).add(new THREE.Mesh(geometryOf(s), m));
    if (s.node.startsWith('Mote')) moteMaterials.set(part(s.node), m);
    else if (s.node.startsWith('Glint')) glintMaterials.set(part(s.node), m);
    else if (s.node === 'Inner') inner.push(m);
    else if (s.node === 'Heart') heart.push(m);
    else if (s.node.startsWith('Aura')) aura[s.node === 'Aura1' ? 1 : 0].push(m);
    else glowing.push(m);
    if (drifts(s.source)) flowing.push(m);  // the shimmer drifts up the curtain, as the plugin moves it
  }
  world.add(top);
  const named = prefix => [...groups.keys()].filter(k => k.startsWith(prefix) && !data.nodes.find(x => x.name === k).billboard).sort();
  const auras = named('Aura').map(k => groups.get(k));
  const motes = named('Mote').map(k => ({ node: groups.get(k), material: moteMaterials.get(k) }));
  const glints = named('Glint').map(k => ({ node: groups.get(k), material: glintMaterials.get(k) }));
  // Wide Circle III's shaders start unseen in the mesh; the frames give them their strength.
  for (const m of [...inner, ...heart, ...glints.map(g => g.material)]) m.uniforms.tint.value.w = 1;
  return { top, circle: groups.get('Circle'), runes: groups.get('Runes'), inner: groups.get('Inner'), auras, motes, glints, glowing, aura, flowing, innerGlow: inner, heartGlow: heart };
}
const circle = buildCircle(circleMesh);

// Elemental Burst: each element's mesh as a node tree, every shader starting unseen as in the game,
// the plugin's frames setting colour and strength.
function buildBlast(data) {
  const groups = new Map(), top = new THREE.Group();
  for (const n of data.nodes) { const g = new THREE.Group(); g.position.set(...n.translate); groups.set(n.name, g); if (n.billboard) billboards.push(g); }
  for (const n of data.nodes) (n.parent ? groups.get(n.parent) : top).add(groups.get(n.name));
  const part = name => { const at = data.nodes.find(x => x.name === name); return at && at.billboard ? at.parent : name; };  // a sprite's own part, past its billboard
  const materials = new Map(), flowing = [];
  for (const s of data.shapes) {
    const m = glowMaterial(s);
    m.uniforms.tint.value.w = 1;
    m.uniforms.opacity.value = 0;
    groups.get(s.node).add(new THREE.Mesh(geometryOf(s), m));
    if (!materials.has(part(s.node))) materials.set(part(s.node), []);
    materials.get(part(s.node)).push(m);
    if (drifts(s.source)) flowing.push(m);  // the texture drifts, as the plugin moves it
  }
  top.visible = false;
  world.add(top);
  return { top, groups, materials, flowing };
}
const blasts = { fire: buildBlast(fireMesh), frost: buildBlast(frostMesh), shock: buildBlast(shockMesh) };
const warHeads = { sabre: buildBlast(warSabreMesh), dragon: buildBlast(warDragonMesh) };
const bears = { furred: buildBlast(bearMesh), plain: buildBlast(bearPlainMesh), red: buildBlast(bearRedMesh), redPlain: buildBlast(bearRedPlainMesh) };
const chaosModels = { plain: buildBlast(chaosMesh), grand: buildBlast(chaosGrandMesh) };
const chaosAffected = [[-115, 100], [112, 74], [12, -135]].map(at => {
  const body = capsule(0x929ca8, 1), binding = buildBlast(chaosBindingMesh);
  body.castShadow = true; body.visible = false; world.add(body); shapeBody(body, 128, 18, at);
  return { at, body, binding };
});
const rejuvenation = buildBlast(rejuvenationMesh);
const sunder = buildBlast(sunderMesh);

// A neutral armored opponent for reading the fracture against a solid surface. This reference
// body is preview-only; the game attaches Mark to the actual opponent's chest.
function sunderOpponent() {
  const body = new THREE.Group();
  const chestHeight = 80;
  const metalShape = sunderMesh.shapes.find(s => /SunderMetal/i.test(s.source));
  const metal = new THREE.MeshStandardMaterial({ color: 0xd6dfeb, map: metalShape ? texture(metalShape.diffuse, true) : null, roughness: 0.6, metalness: 0.22 });
  const trim = new THREE.MeshStandardMaterial({ color: 0xaab7c6, roughness: 0.55, metalness: 0.3 });
  const cloth = new THREE.MeshStandardMaterial({ color: 0x282a2e, roughness: 0.95 });
  const leather = new THREE.MeshStandardMaterial({ color: 0x352d27, roughness: 0.9 });
  const add = (geometry, material, at, size) => {
    const mesh = new THREE.Mesh(geometry, material);
    mesh.position.set(...at); if (size) mesh.scale.set(...size);
    mesh.castShadow = true; mesh.receiveShadow = true; body.add(mesh); return mesh;
  };
  const ellipsoid = (material, at, size) => add(new THREE.SphereGeometry(1, 28, 18), material, at, size);
  const limb = (material, from, to, r1, r2) => {
    const a = new THREE.Vector3(...from), b = new THREE.Vector3(...to), delta = b.clone().sub(a);
    const mesh = add(new THREE.CylinderGeometry(r2, r1, delta.length(), 18), material, a.clone().add(b).multiplyScalar(0.5).toArray());
    mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),delta.normalize()); return mesh;
  };
  for (const side of [-1,1]) {
    ellipsoid(leather,[side*11,3,5],[8,13,5]);
    limb(cloth,[side*11,0,9],[side*10,0,51],7,10);
    limb(metal,[side*11,2,10],[side*10,1,30],7.5,8.3);
    ellipsoid(trim,[side*10,5,32],[8,7,7]);
    limb(cloth,[side*27,0,100],[side*34,0,76],7.5,6.2);
    limb(metal,[side*34,0,76],[side*37,5,57],6.7,5);
    ellipsoid(metal,[side*29,0,103],[11,12,8]);
    ellipsoid(leather,[side*38,5,52],[5,6,7]);
  }
  ellipsoid(cloth,[0,0,65],[24,14,28]);
  ellipsoid(leather,[0,0,46],[22,13,9]);
  // The cracked shell is x +/-27, y +/-18. The plate sits just inside that shell.
  const positions=[], uv=[], indices=[], segments=64, rows=16;
  for(let row=0;row<=rows;row++) {
    const z=-32+61*row/rows, u=(z+32)/61;
    const width=21+4*Math.sin(Math.PI*u), depth=13.5+2.5*Math.sin(Math.PI*u);
    for(let i=0;i<=segments;i++) {
      const a=i/segments*Math.PI*2;
      positions.push(Math.sin(a)*width,Math.cos(a)*depth,z+chestHeight);
      uv.push(i/segments,row/rows);
      if(row<rows && i<segments) {
        const n=row*(segments+1)+i;
        indices.push(n,n+segments+1,n+1,n+1,n+segments+1,n+segments+2);
      }
    }
  }
  const plate=new THREE.BufferGeometry();
  plate.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));
  plate.setAttribute('uv',new THREE.Float32BufferAttribute(uv,2)); plate.setIndex(indices); plate.computeVertexNormals();
  add(plate,metal,[0,0,0]);
  limb(trim,[0,0,106],[0,0,113],9,8);
  ellipsoid(metal,[0,0,119],[11,10.5,13]);
  // A dark visor makes the helmet read at the same scale as the breastplate.
  add(new THREE.BoxGeometry(15,1.2,3.4),cloth,[0,9.5,120]);
  const helmetRidge=add(new THREE.BoxGeometry(2.5,19,11),trim,[0,0,126]);
  helmetRidge.rotation.x=-0.12;
  body.visible=false; world.add(body);
  return {body,chestHeight};
}
const sunderTarget=sunderOpponent();
sunder.groups.get('Mark').position.z=sunderTarget.chestHeight;
sunder.groups.get('Burst').position.z=sunderTarget.chestHeight;
const weakWillModels = { plain: buildBlast(weakWillMesh), grand: buildBlast(weakWillGrandMesh) };
const weakWillTank = sunderOpponent();
const bastionModels = { plain: buildBlast(bastionMesh), grand: buildBlast(bastionGrandMesh) };
for(const model of Object.values(bastionModels)) {
  model.ownedMaterials=new Map();
  for(const name of bastionLook.names){const materials=[];model.groups.get(name).traverse(n=>{if(n.isMesh)materials.push(n.material);});model.ownedMaterials.set(name,materials);}
}
for (const model of Object.values(weakWillModels)) {
  model.ownedMaterials = new Map();
  for (const name of weakWillLook.names) {
    const materials = [];
    model.groups.get(name).traverse(node => { if (node.isMesh) materials.push(node.material); });
    model.ownedMaterials.set(name, materials);
  }
}

const audioData=await fetch('out/audio-plans.json').then(r=>{if(!r.ok)throw Error('Preview sound data missing');return r.json();});
const soundPlayer=new SoundPlayer(audioData,$('audioPlayers'),$('effectSound'),$('audioStatus'));
function configureAudio(){
  const common=audioData.existing,cue=(form,at,seconds,volume)=>({form,at,seconds,volume});
  let rank=1,life=gone,plan=[];
  if(mode==='bastion'){
    rank=Number($('bastionRank').value);life=bastionRun().duration;
    plan=bastionLook.audio.map(c=>cue(rank>=3?c.grandForm:c.form,c.at,c.seconds,c.volume[rank-1]));
  }else if(Object.hasOwn(audioData.plans,mode)){
    if(mode==='weakwill'){rank=Number($('weakWillRank').value);life=weakWillRun().duration;}
    if(mode==='rejuvenation'){rank=Number($('rejuvenationRank').value);life=rejuvenationLook.ranks[rank-1].seconds;}
    if(mode==='wolf')rank=$('wolfRank').value==='grand'?3:1;
    if(mode==='warcry'){rank=Number($('warCryRank').value);life=bearLook.seconds;}
    if(mode==='taunt'){rank=$('bearRank').value==='grand'?3:1;life=bearLook.seconds;}
    if(mode==='circle')rank=$('healRank').value==='3'||Number($('reach').value)===180?3:1;
    if(mode==='renewal')rank=$('renewalRank').value==='grand'?3:1;
    if(mode==='chaos'){rank=$('chaosRank').value==='55'?3:1;life=chaosBindingSeconds();}
    if(mode==='magnus')rank=$('magnusRank').value==='grand'?3:1;
    if(mode==='sunder')rank=$('sunderRank').value==='grand'?3:1;
    if(mode==='rain')rank=$('rainRank').value==='grand'?3:1;
    plan=audioData.plans[mode][Math.max(0,Math.min(2,rank-1))].map(c=>({...c}));
    if(mode==='taunt'||mode==='warcry')for(const c of plan)c.at+=bearLook.roar;
    if(mode==='wolf')plan.push(cue(common.blow,pose.beats.snap,.8,.7),cue(common.slam,pose.beats.snap,1.4,.6));
    if(mode==='circle'||mode==='renewal')plan.unshift(cue(common.heal,0,1.8,.45));
    if(mode==='rejuvenation')plan.unshift(cue(common.rejuvenate,0,1.5,.40));
    if(mode==='rain')for(let at=.45;at<gone-.8;at+=.65)plan.push({...audioData.plans.arrow[0][0],at});
  }else if(mode==='taunt')plan=[cue(common.bear,bearLook.roar,3,.75)];
  
  else if(mode==='quake')plan=[cue(common.slam,0,1.6,.7),cue(common.rocks,0,1.7,.35),cue(common.rumble,0,1.4,.32),cue(common.crumble,-.9,.9,.2)];
  else if(mode==='blast'){
    const element=$('element').value,at=blastNow().data.flight;
    plan=[cue(common[element],at,2,.8),cue(common[element+'After'],at+(element==='frost'?1.15:.15),1.4,.45)];
  }
  if((mode==='quake'&&$('quakeRank').value==='3')||(mode==='blast'&&Number($('tier').value)===blastLook.grandTier)){const start=mode==='blast'?blastNow().data.flight:0;plan.push(...audioData.plans.impact[2].map(c=>({...c,at:c.at+start})));}
  soundPlayer.load(plan,life);
}
for (const bear of [...Object.values(bears), ...Object.values(warHeads)]) {
  bear.groups.get('Head').matrixAutoUpdate = false;
  for (const name of ['Head', 'Jaw']) for (const m of bear.materials.get(name) || []) m.uniforms.tint.value.set(1, 1, 1, 1);
}
const blastLight = new THREE.PointLight(0xffffff, 0, 800, 2);
world.add(blastLight);

// Magnus: the orb's node tree as the mesh has it, every shader unseen until the frames give it its colour
// and strength; the skins' noise drifts and the streams' pours.
function buildMagnus(data) {
  const groups = new Map(), top = new THREE.Group();
  for (const n of data.nodes) { const g = new THREE.Group(); g.position.set(...n.translate); groups.set(n.name, g); if (n.billboard) billboards.push(g); }
  for (const n of data.nodes) (n.parent ? groups.get(n.parent) : top).add(groups.get(n.name));
  const part = name => { const at = data.nodes.find(x => x.name === name); return at && at.billboard ? at.parent : name; };  // a sprite's own part, past its billboard
  const materials = new Map(), skins = [], streams = [];
  for (const s of data.shapes) {
    const m = glowMaterial(s);
    m.uniforms.tint.value.w = 1;
    m.uniforms.opacity.value = 0;
    groups.get(s.node).add(new THREE.Mesh(geometryOf(s), m));
    if (!materials.has(part(s.node))) materials.set(part(s.node), []);
    materials.get(part(s.node)).push(m);
    if (drifts(s.source)) (s.node.startsWith('Tendril') || s.node.startsWith('Veil') ? streams : skins).push(m);
  }
  for (const name of [...groups.keys()].filter(n => /^(Ring|Tendril|Veil|Wisp)/.test(n))) groups.get(name).matrixAutoUpdate = false;
  top.visible = false;
  world.add(top);
  return { top, groups, materials, skins, streams };
}
const magnus = buildMagnus(magnusMesh);
const magnusLight = new THREE.PointLight(0xffffff, 0, magnusLook.lightReach, 2);
world.add(magnusLight);
// The mage, facing north (+Y): a nose shows which way.
const caster = capsule(0x8a7fb8, 1);
caster.castShadow = true;
world.add(caster);
shapeBody(caster, 128, 18, [0, 0]);
const nose = new THREE.Mesh(new THREE.ConeGeometry(6, 18, 12), new THREE.MeshStandardMaterial({ color: 0x8a7fb8 }));
nose.rotation.x = -Math.PI / 2;  // its point along the game's +Y
nose.position.set(0, 20, 110);
world.add(nose);

// Earthshaker: its node tree, the glows and dust unseen until the frames give them strength, the rocks
// lit and textured as the game would draw them, each shape under its own transform.
function buildQuake(data) {
  const groups = new Map(), top = new THREE.Group();
  for (const n of data.nodes) { const g = new THREE.Group(); g.position.set(...n.translate); groups.set(n.name, g); if (n.billboard) billboards.push(g); }
  for (const n of data.nodes) (n.parent ? groups.get(n.parent) : top).add(groups.get(n.name));
  const part = name => { const at = data.nodes.find(x => x.name === name); return at && at.billboard ? at.parent : name; };  // a sprite's own part, past its billboard
  const materials = new Map(), flowing = [];
  for (const s of data.shapes) {
    const m = s.effect ? glowMaterial(s) : new THREE.MeshStandardMaterial({
      map: texture(s.diffuse, true), normalMap: texture(s.normal, false), normalScale: new THREE.Vector2(1, -1), vertexColors: true, roughness: 0.9, metalness: 0
    });
    if (s.effect) { m.uniforms.tint.value.w = 1; m.uniforms.opacity.value = 0; }
    const mesh = new THREE.Mesh(geometryOf(s), m);
    const { translate: t, rotate: r, scale: k } = s.transform;
    mesh.matrixAutoUpdate = false;
    mesh.matrix.set(r[0] * k, r[1] * k, r[2] * k, t[0], r[3] * k, r[4] * k, r[5] * k, t[1], r[6] * k, r[7] * k, r[8] * k, t[2], 0, 0, 0, 1);
    mesh.castShadow = !s.effect;
    groups.get(s.node).add(mesh);
    if (!materials.has(part(s.node))) materials.set(part(s.node), []);
    materials.get(part(s.node)).push(m);
    if (drifts(s.source)) flowing.push(m);
  }
  for (const name of quakeLook.parts) groups.get(name).matrixAutoUpdate = false;
  top.visible = false;
  world.add(top);
  return { top, groups, materials, flowing };
}
const quake = buildQuake(quakeMesh);
// The bruiser at the middle, and enemies round them within the reach (one just outside it).
const bruiser = capsule(0x6f86a8, 1);
bruiser.castShadow = true;
world.add(bruiser);
shapeBody(bruiser, 128, 20, [0, 0]);
const foes = [[150, 60], [-120, 140], [60, -190], [-210, -90], [230, 200], [420, -60]].map(at => {
  const body = capsule(0x9a948a, 1);
  body.castShadow = true;
  world.add(body);
  shapeBody(body, 128, 20, at);
  return { at, body };
});

// Arrow Rain: its node tree, every shader unseen until the frames give it strength; the dust laid over
// the scene, the rest glowing. The archer off to one side, and enemies bunched under the mark.
function buildRain(data) {
  const groups = new Map(), top = new THREE.Group();
  for (const n of data.nodes) { const g = new THREE.Group(); g.position.set(...n.translate); groups.set(n.name, g); if (n.billboard) billboards.push(g); }
  for (const n of data.nodes) (n.parent ? groups.get(n.parent) : top).add(groups.get(n.name));
  const part = name => { const at = data.nodes.find(x => x.name === name); return at && at.billboard ? at.parent : name; };  // a sprite's own part, past its billboard
  const materials = new Map(), flowing = [];
  for (const s of data.shapes) {
    const m = glowMaterial(s);
    m.uniforms.tint.value.w = 1;
    m.uniforms.opacity.value = 0;
    groups.get(s.node).add(new THREE.Mesh(geometryOf(s), m));
    if (!materials.has(part(s.node))) materials.set(part(s.node), []);
    materials.get(part(s.node)).push(m);
    if (drifts(s.source)) flowing.push(m);
  }
  top.visible = false;
  world.add(top);
  return { top, groups, materials, flowing };
}
const rain = buildRain(rainMesh);
const archer = capsule(0x7fa86f, 1);
archer.castShadow = true;
world.add(archer);
shapeBody(archer, 128, 18, [rainLook.bow[0] - 15, rainLook.bow[1] - 15]);
const huddle = [[0, 0], [90, 60], [-110, 40], [40, -130], [-60, -80], [180, -40], [-200, 150]].map(at => {
  const body = capsule(0x9a948a, 1);
  body.castShadow = true;
  world.add(body);
  shapeBody(body, 128, 20, at);
  return { at, body };
});

// Renewal: the cloak's node tree (built as the rain's is), the healer in the middle wearing it and three
// hurt companions round them that its pulses wash.
const cloak = buildRain(cloakMesh);
const cloakLight = new THREE.PointLight(0xffffff, 0, cloakLook.lightReach, 2);
world.add(cloakLight);
const wearer = capsule(0xd8d4cc, 1);
wearer.castShadow = true;
world.add(wearer);
shapeBody(wearer, 128, 18, [0, 0]);
const washedOnes = cloakLook.allies.map(at => {
  const body = capsule(0x6fa87a, 1);
  body.castShadow = true;
  world.add(body);
  shapeBody(body, 128, 20, at);
  return { at, body };
});

// The mage who casts it, the enemy it is aimed at, and others round about: one near, one at the edge
// of the widest reach, one beyond any.
const mage = capsule(0x6f86a8, 1);
mage.castShadow = true;
world.add(mage);
shapeBody(mage, 128, 18, [blastLook.elements[0].from[0] - 20, blastLook.elements[0].from[1] - 20]);
const targets = [
  { name: 'the target', at: [0, 0] },
  { name: 'one beside it', at: [120, 70] },
  { name: 'one further off', at: [-80, 225] },
  { name: 'one well away', at: [330, -260] }
].map(who => { const body = capsule(0x9a948a, 1); body.castShadow = true; world.add(body); shapeBody(body, 128, 20, who.at); return { ...who, body }; });

// Who stands round it: you and a hurt companion in it, the healer outside, an undead in it burning
// and one outside untouched.
const standing = [
  { name: 'you', at: [0, 0], height: 128, radius: 20, color: 0x6f86a8, alive: true },
  { name: 'a companion', at: [150, 90], height: 128, radius: 20, color: 0x6fa87a, alive: true },
  { name: 'the healer', at: [-480, -260], height: 128, radius: 20, color: 0xd8d4cc, alive: true },
  { name: 'an undead', at: [190, -120], height: 120, radius: 18, color: 0x8a6fa8, alive: false },
  { name: 'an undead', at: [560, 160], height: 120, radius: 18, color: 0x8a6fa8, alive: false }
].map(who => { const body = capsule(who.color, 1); body.castShadow = true; world.add(body); shapeBody(body, who.height, who.radius, who.at); return { ...who, body }; });

// --- The enemy and the fighter ------------------------------------------------------------------

function capsule(color, opacity) {
  return new THREE.Mesh(new THREE.CapsuleGeometry(1, 1, 6, 16),
    new THREE.MeshStandardMaterial({ color, roughness: 0.9, transparent: opacity < 1, opacity }));
}
const enemy = capsule(0x9a948a, 0.92);
enemy.castShadow = true;
world.add(enemy);
const fighter = capsule(0x6f86a8, 1);
fighter.castShadow = true;
world.add(fighter);
let enemyHeight = 128, enemyBreadth = 20;
function shapeBody(mesh, height, radius, at) {
  mesh.scale.set(radius, (height - 2 * radius), radius);
  mesh.geometry.dispose();
  mesh.geometry = new THREE.CapsuleGeometry(radius, Math.max(height - 2 * radius, 1), 6, 16);
  mesh.scale.set(1, 1, 1);
  mesh.rotation.set(Math.PI / 2, 0, 0);  // the capsule's own axis (Y) up the game's Z
  mesh.position.set(at[0], at[1], height / 2);
}
// The ghostly smoke on whoever the bite holds: the mesh's first slot, round the enemy.
const haunt = buildRain(hauntMesh);
for (let k = 1; k < 6; k++) haunt.groups.get(`Haunt${k}`).scale.setScalar(0.0001);
// Tipped back by a_recoil (0..1 of a stagger's recoil) away from the fighter, about their feet.
function tipEnemy(recoil) {
  const a = 0.38 * recoil, h = enemyHeight;
  enemy.rotation.set(Math.PI / 2 - a, 0, 0);
  enemy.position.set(0, Math.sin(a) * h / 2, Math.cos(a) * h / 2);
  haunt.groups.get('Haunt0').rotation.x = -Math.min(a, pose.haunt.lean);  // the smoke leans with them, as Haunt::Lean has it
}
function setEnemy(height, breadth) {
  enemyHeight = height; enemyBreadth = breadth;
  shapeBody(enemy, height, breadth, [0, 0]);
  judge();
}
shapeBody(fighter, 128, 18, [0, -150]);

// --- Capsules and the bite's verdict, as the plugin tests them ---------------------------------

const headCapsule = new THREE.Mesh(new THREE.CapsuleGeometry(1, 1, 4, 12), new THREE.MeshBasicMaterial({ color: 0xee703c, wireframe: true, transparent: true, opacity: 0.6 }));
const bodyCapsule = new THREE.Mesh(new THREE.CapsuleGeometry(1, 1, 4, 12), new THREE.MeshBasicMaterial({ color: 0x4fd18b, wireframe: true, transparent: true, opacity: 0.6 }));
world.add(headCapsule, bodyCapsule);

const place = (f, p) => {
  const r = f.r, s = f.s;
  return [r[0] * p[0] * s + r[1] * p[1] * s + r[2] * p[2] * s + f.p[0], r[3] * p[0] * s + r[4] * p[1] * s + r[5] * p[2] * s + f.p[1], r[6] * p[0] * s + r[7] * p[1] * s + r[8] * p[2] * s + f.p[2]];
};
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
function segmentGap(p0, p1, q0, q1) {
  const d1 = sub(p1, p0), d2 = sub(q1, q0), r = sub(p0, q0);
  const a = dot(d1, d1), e = dot(d2, d2), f = dot(d2, r);
  let s = 0, t = 0;
  if (a <= 1e-4 && e <= 1e-4) return Math.sqrt(dot(r, r));
  if (a <= 1e-4) t = Math.min(Math.max(f / e, 0), 1);
  else {
    const c = dot(d1, r);
    if (e <= 1e-4) s = Math.min(Math.max(-c / a, 0), 1);
    else {
      const b = dot(d1, d2), denom = a * e - b * b;
      s = denom > 1e-4 ? Math.min(Math.max((b * f - c * e) / denom, 0), 1) : 0;
      t = (b * s + f) / e;
      if (t < 0) { t = 0; s = Math.min(Math.max(-c / a, 0), 1); } else if (t > 1) { t = 1; s = Math.min(Math.max((b - c) / a, 0), 1); }
    }
  }
  const u = [p0[0] + d1[0] * s - q0[0] - d2[0] * t, p0[1] + d1[1] * s - q0[1] - d2[1] * t, p0[2] + d1[2] * s - q0[2] - d2[2] * t];
  return Math.sqrt(dot(u, u));
}
function stretch(mesh, a, b, radius) {
  const from = new THREE.Vector3(...a), to = new THREE.Vector3(...b);
  const length = from.distanceTo(to);
  mesh.geometry.dispose();
  mesh.geometry = new THREE.CapsuleGeometry(radius, Math.max(length, 0.01), 4, 12);
  mesh.position.copy(from).add(to).multiplyScalar(0.5);
  mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), to.clone().sub(from).normalize());
}
let biteLands = false;
function judge() {
  const visual = frameAt(pose.beats.snap), snap = {...visual,...visual.hit};
  const back = place(snap, pose.points.back), front = place(snap, pose.points.front);
  const radius = pose.radius * snap.s;
  const low = [0, 0, enemyBreadth], top = [0, 0, Math.max(enemyHeight * 0.92, enemyBreadth)];
  const gap = segmentGap(back, front, low, top);
  biteLands = gap <= radius + enemyBreadth;
  $('snapAt').textContent = pose.beats.snap.toFixed(2);
  $('verdict').innerHTML = `${biteLands ? '<span class="yes">Lands</span>' : '<span class="no">Misses</span>'}: gap ${gap.toFixed(0)} against reach ${(radius + enemyBreadth).toFixed(0)} (head ${(radius * 2).toFixed(0)} across)`;
  stretch(bodyCapsule, low, top, enemyBreadth);
}

// --- Playback --------------------------------------------------------------------------------

let mode = 'wolf';
// The first ranks' bite, or the third's red wolf, which holds on longer.
const wolfRun = () => ($('wolfRank').value === 'grand' ? pose.grand : pose.plain);
let gone = pose.plain.end;
function frameAt(t) {
  const run = wolfRun();
  const x = Math.min(Math.max(t, 0), run.end) * pose.fps;
  const i = Math.min(Math.floor(x), run.frames.length - 1), j = Math.min(i + 1, run.frames.length - 1), k = x - i;
  const a = run.frames[i], b = run.frames[j], mix = (u, v) => u + (v - u) * k;
  return { t, r: a.r.map((v, n) => mix(v, b.r[n])), p: a.p.map((v, n) => mix(v, b.p[n])), s: mix(a.s, b.s), jaw: mix(a.jaw, b.jaw), a: mix(a.a, b.a), recoil: mix(a.recoil, b.recoil), haunt: a.haunt,
    hit:{p:a.hit.p.map((v,n)=>mix(v,b.hit.p[n])),s:mix(a.hit.s,b.hit.s)} };
}
function beats(list) {
  document.querySelectorAll('.beat').forEach(el => el.remove());
  for (const [at, label, key] of list) {
    const el = document.createElement('div');
    el.className = `beat ${key ? 'snap' : ''}`;
    el.style.left = `${at / gone * 100}%`;
    el.textContent = `${label} ${at.toFixed(2)}`;
    $('timeline').appendChild(el);
  }
  $('total').textContent = gone.toFixed(2);
}
const wolfBeats = () => {
  const run = wolfRun(), held = pose.beats.snap + pose.beats.catch;
  return [[pose.beats.tense, 'tense'], [pose.beats.snap, 'BITE', true], [held, 'frozen'], [run.shake, 'let go'], [held + run.held, 'free']];
};
const circleBeats = () => [[circleLook.rise, 'aura up'], [circleLook.seconds - circleLook.sink, 'sinking', true], [circleLook.seconds, 'gone']];
const phaseOf = t => { const run = wolfRun(); return t < pose.beats.rise ? 'taking shape' : t < pose.beats.tense ? 'rearing' : t < pose.beats.snap ? 'slamming shut' : t < run.shake ? 'shaking its catch' : t < run.gone ? 'letting go' : t < pose.beats.snap + pose.beats.catch + run.held ? 'held frozen' : 'free'; };

function lookAt(t) {
  const x = Math.min(Math.max(t, 0), circleLook.seconds) * circleLook.fps;
  const i = Math.min(Math.floor(x), circleLook.frames.length - 1), j = Math.min(i + 1, circleLook.frames.length - 1), k = x - i;
  const a = circleLook.frames[i], b = circleLook.frames[j], mix = (u, v) => u + (v - u) * k;
  return { ring: mix(a.ring, b.ring), turn: mix(a.turn, b.turn), pulse: mix(a.pulse, b.pulse), aura: mix(a.aura, b.aura), waves: a.waves.map((v, n) => mix(v, b.waves[n])),
    edgeHeight: a.edgeHeight.map((v, n) => Math.abs(b.edgeHeight[n] - v) > 10 ? v : mix(v, b.edgeHeight[n])),
    edgeAlpha: a.edgeAlpha.map((v, n) => mix(v, b.edgeAlpha[n])),
    inner: mix(a.inner, b.inner), innerTurn: mix(a.innerTurn, b.innerTurn), heart: mix(a.heart, b.heart),
    glints: a.glints.map((m, n) => (Math.abs(b.glints[n][2] - m[2]) > 10 ? m : m.map((v, q) => mix(v, b.glints[n][q])))),
    motes: a.motes.map((m, n) => (Math.abs(b.motes[n][2] - m[2]) > 10 ? m : m.map((v, q) => mix(v, b.motes[n][q])))) };  // no smear across a mote's wrap
}
function showCircle(t) {
  const look = lookAt(t);
  const radius = circleLook.radius + Number($('reach').value);
  circle.circle.scale.setScalar(radius / circleLook.meshRadius);
  circle.runes.rotation.z = look.turn; circle.top.position.z = 6;
  circle.auras.forEach((a, i) => { a.rotation.z = look.waves[i]; a.position.z = look.edgeHeight[i] - circleLook.auraSunk * (1 - look.aura); });
  circle.aura.forEach((materials, i) => { for (const m of materials) m.uniforms.opacity.value = 1.65 * look.edgeAlpha[i]; });
  for (const m of circle.flowing) m.uniforms.uvOffset.value.y = t * circleLook.auraFlow;
  // Wide Circle III: the inner circle turning against the runes, the white heart, specks rising all over.
  const grand = $('healRank').value === '3' || Number($('reach').value) === 180;
  circle.inner.rotation.z = look.innerTurn;
  for (const m of circle.innerGlow) m.uniforms.opacity.value = grand ? 1.65 * look.inner : 0;
  for (const m of circle.heartGlow) m.uniforms.opacity.value = grand ? 1.65 * look.heart : 0;
  circle.glints.forEach((g, i) => {
    const [x, y, z, size, alpha] = look.glints[i];
    g.node.position.set(x, y, z);
    g.node.scale.setScalar(Math.max(size, 0.0001));
    g.material.uniforms.opacity.value = grand ? 1.65 * alpha : 0;
  });
  circle.motes.forEach((m, i) => {
    const [x, y, z, size, alpha] = look.motes[i];
    m.node.position.set(x, y, z);
    m.node.scale.setScalar(Math.max(size, 0.0001));
    m.material.uniforms.opacity.value = 1.65 * alpha;
  });
  for (const m of circle.glowing) m.uniforms.opacity.value = 1.65 * look.ring * (0.85 + 0.15 * look.pulse);
  const inside = [];
  for (const who of standing) {
    const within = Math.hypot(...who.at) <= radius && look.ring > 0.2;
    if (within) inside.push(who.alive ? who.name : `${who.name} (burning)`);
    const glow = within ? (who.alive ? 0x5a4a10 : 0x6a1010) : 0x000000;
    who.body.material.emissive.setHex(glow);
    who.body.material.emissiveIntensity = within ? 0.6 + 0.4 * look.pulse : 0;
  }
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < circleLook.rise ? 'rising' : t < circleLook.seconds - circleLook.sink ? 'standing' : 'sinking';
  $('line1').innerHTML = `radius <b>${radius.toFixed(0)}</b> &middot; glow <b>${(look.ring * 100).toFixed(0)}</b>%`;
  $('line2').innerHTML = `in it: <b>${inside.length ? inside.join(', ') : 'nobody'}</b>`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

// The blast: the light flying from the hand to the feet, then the element's frames from the moment it
// strikes. Frames are taken as they come (60 a second), not blended, so the lightning flickers as in game.
const ELEMENTS = ['fire', 'frost', 'shock'];
function blastNow() {
  const index = ELEMENTS.indexOf($('element').value);
  const tier = Number($('tier').value), data = blastLook.elements[index];
  // The third tier plays the grand frames, with its own parts.
  return { name: ELEMENTS[index], data, look: tier === blastLook.grandTier ? data.grand : data.plain, mesh: blasts[ELEMENTS[index]], radius: blastLook.radius[tier] };
}
// A facing part (the lightning's strips): its length along yaw and tilt, turned about it to face the
// camera, as Blast::Facing does in the game.
const facePart = (group, p, size) => {
  const ct = Math.cos(p[4]), st = Math.sin(p[4]);
  const axis = new THREE.Vector3(Math.cos(p[3]) * st, Math.sin(p[3]) * st, ct);
  const toCamera = new THREE.Vector3(camera.position.x, -camera.position.z, camera.position.y).sub(new THREE.Vector3(p[0], p[1], p[2]).multiplyScalar(size));
  const face = toCamera.sub(axis.clone().multiplyScalar(toCamera.dot(axis)));
  if (face.lengthSq() < 1e-8) face.set(-axis.y, axis.x, 0);
  face.normalize();
  group.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(new THREE.Vector3().crossVectors(face, axis), face, axis));
};
const placePart = (group, p) => {
  group.position.set(p[0], p[1], p[2]);
  group.rotation.set(0, p[4], p[3], 'ZYX');  // its +Z leant toward the heading, then turned to it
  group.scale.setScalar(Math.max(p[5], 0.0001));
};
const paint = (list, alpha, rgb) => { for (const m of list || []) { m.uniforms.opacity.value = alpha; m.uniforms.tint.value.set(rgb[0], rgb[1], rgb[2], 1); } };
function showBlast(t) {
  const { name, data, look, mesh, radius } = blastNow();
  for (const [key, b] of Object.entries(blasts)) b.top.visible = key === name;
  const orbGroup = mesh.groups.get('Orb'), blastGroup = mesh.groups.get('Blast');
  const age = t - data.flight;
  // The light on its way.
  if (age < 0) {
    const o = data.orb[Math.min(Math.round(t / data.flight * (data.orb.length - 1)), data.orb.length - 1)];
    placePart(orbGroup, [o[0], o[1], o[2], o[3], o[4], 1]);
    for (const [node, list] of mesh.materials) if (node === 'Orb') paint(list, o[5], [o[6], o[7], o[8]]);
    blastLight.position.copy(new THREE.Vector3(o[0], o[1], o[2]));
    blastLight.color.setRGB(o[6], o[7], o[8]);
    blastLight.intensity = 0.35 * o[5] * blastLook.lightFade * 60000;
  } else {
    paint(mesh.materials.get('Orb'), 0, [1, 1, 1]);
  }
  // The blast.
  blastGroup.scale.setScalar(radius / blastLook.meshRadius);
  const f = look.frames[Math.min(Math.max(Math.round(age * blastLook.fps), 0), look.frames.length - 1)];
  data.parts.forEach((part, i) => {
    const p = f.parts[i];
    placePart(mesh.groups.get(part), p);
    if (p[10]) facePart(mesh.groups.get(part), p, radius / blastLook.meshRadius);
    paint(mesh.materials.get(part), age < 0 ? 0 : p[6], [p[7], p[8], p[9]]);
    // Attached frost veils have their own gentle breath and lift, sampled from
    // the same native animation as the ice; their mist never scrolls the ice texture.
    const auraName = `${part}Aura`, aura = mesh.groups.get(auraName);
    if (aura) {
      aura.position.set(0, 0, p[12] || 0);
      aura.rotation.set(0, 0, p[13] || 0);
      paint(mesh.materials.get(auraName), age < 0 ? 0 : (p[11] || 0), [p[7], p[8], p[9]]);
    }
  });
  for (const m of mesh.flowing) m.uniforms.uvOffset.value.y = -Math.max(age, 0) * blastLook.flow;
  if (age >= 0) {
    blastLight.position.set(0, 0, 60);
    blastLight.color.setRGB(...f.lightColor);
    blastLight.intensity = f.light * blastLook.lightFade * 60000;
  }
  blastLight.distance = radius * blastLook.lightReach;
  // Who it strikes, and how hard, as the plugin reckons it.
  const hit = [];
  for (const who of targets) {
    const d = Math.hypot(...who.at);
    const struck = d <= radius && age >= 0;
    const share = d / radius <= blastLook.centre ? 1 : 1 - (1 - blastLook.edge) * Math.min((d / radius - blastLook.centre) / (1 - blastLook.centre), 1);
    if (struck) hit.push(`${who.name} ${(share * 100).toFixed(0)}%`);
    who.body.material.emissive.setHex(struck && age < 0.25 ? 0x902818 : 0x000000);
    who.body.material.emissiveIntensity = struck && age < 0.25 ? 1 : 0;
  }
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = age < 0 ? 'flying' : age < 0.3 ? 'BLAST' : 'fading';
  $('line1').innerHTML = `${name} &middot; reach <b>${radius.toFixed(0)}</b> &middot; light <b>${((age < 0 ? 0 : f.light) * 100).toFixed(0)}</b>%`;
  $('line2').innerHTML = `struck: <b>${hit.length ? hit.join(', ') : age < 0 ? '-' : 'nobody'}</b>`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}
const blastBeats = () => { const { data, look } = blastNow(); return [[0, 'cast'], [data.flight, 'STRIKES', true], [data.flight + look.seconds, 'gone']]; };

function setScene(name) {
  soundPlayer.reset();
  mode = name;
  const taunt = name === 'taunt', warcry = name === 'warcry';
  const binding = name === 'chaos';
  for (const affected of chaosAffected) affected.body.visible = affected.binding.top.visible = false;
  const rejuvenating = name === 'rejuvenation';
  const sundering = name === 'sunder';
  const weakening = name === 'weakwill', guarding = name === 'bastion';
  for (const model of Object.values(bastionModels)) model.top.visible = false;
  for (const model of Object.values(weakWillModels)) model.top.visible = false;
  weakWillTank.body.visible = weakening || guarding; weakWillTank.body.position.set(0,0,0);
  sunder.top.visible = sunderTarget.body.visible = sundering;
  for (const chaos of Object.values(chaosModels)) chaos.top.visible = false;
  for (const bear of [...Object.values(bears), ...Object.values(warHeads)]) bear.top.visible = false;
  const wolf = name === 'wolf', healing = name === 'circle', blast = name === 'blast', drawing = name === 'magnus', shaking = name === 'quake', raining = name === 'rain', renewing = name === 'renewal';
  for (const h of Object.values(heads)) h.head.visible = false;
  enemy.visible = fighter.visible = wolf;
  haunt.top.visible = wolf;
  if (!wolf) tipEnemy(0);
  headCapsule.visible = bodyCapsule.visible = false;
  circle.top.visible = healing;
  rejuvenation.top.visible = rejuvenating;
  for (const who of standing) who.body.visible = healing;
  for (const b of Object.values(blasts)) b.top.visible = false;
  mage.visible = blast;
  for (const who of targets) who.body.visible = blast;
  blastLight.intensity = 0;
  magnus.top.visible = nose.visible = drawing;
  caster.visible = drawing || rejuvenating;
  quake.top.visible = bruiser.visible = shaking;
  bruiser.visible = shaking || taunt || warcry || (binding && $('chaosView').value !== 'target');
  for (const foe of foes) foe.body.visible = shaking;
  rain.top.visible = archer.visible = raining;
  cloak.top.visible = wearer.visible = renewing;
  for (const who of washedOnes) who.body.visible = renewing;
  cloakLight.intensity = 0;
  for (const foe of huddle) foe.body.visible = raining;
  magnusLight.intensity = 0;
  document.querySelectorAll('.wolf').forEach(el => { el.style.display = wolf ? '' : 'none'; });
  document.querySelectorAll('.circle').forEach(el => { el.style.display = healing ? '' : 'none'; });
  document.querySelectorAll('.rejuvenation').forEach(el => { el.style.display = rejuvenating ? '' : 'none'; });
  document.querySelectorAll('.blast').forEach(el => { el.style.display = blast ? '' : 'none'; });
  document.querySelectorAll('.magnus').forEach(el => { el.style.display = drawing ? '' : 'none'; });
  document.querySelectorAll('.quake').forEach(el => { el.style.display = shaking ? '' : 'none'; });
  document.querySelectorAll('.rain').forEach(el => { el.style.display = raining ? '' : 'none'; });
  document.querySelectorAll('.renewal').forEach(el => { el.style.display = renewing ? '' : 'none'; });
  document.querySelectorAll('.warcry').forEach(el => { el.style.display = warcry ? '' : 'none'; });
  document.querySelectorAll('.taunt').forEach(el => { el.style.display = taunt ? '' : 'none'; });
  document.querySelectorAll('.chaos').forEach(el => { el.style.display = binding ? '' : 'none'; });
  document.querySelectorAll('.sunder').forEach(el => { el.style.display = sundering ? '' : 'none'; });
  document.querySelectorAll('.bastion').forEach(el => { el.style.display = guarding ? '' : 'none'; });
  document.querySelectorAll('.weakwill').forEach(el => { el.style.display = weakening ? '' : 'none'; });
  $('shake').parentElement.style.display = wolf || taunt || warcry || guarding ? '' : 'none';
  $('bite').style.display = wolf ? '' : 'none';
  gone = wolf ? wolfRun().end : healing ? circleLook.seconds : drawing ? magnusRun().seconds : shaking ? quakeLength() : raining ? rainRun().seconds : renewing ? cloakLook.seconds : blastNow().data.flight + blastNow().look.seconds;
  beats(wolf ? wolfBeats() : healing ? circleBeats() : drawing ? magnusBeats() : shaking ? quakeBeats() : raining ? rainBeats() : renewing ? cloakBeats() : blastBeats());
  if (taunt || warcry) { gone = bearLook.seconds; beats([[bearLook.roar, 'ROAR', true], [bearLook.fade, 'fade'], [gone, 'gone']]); }
  if (binding) { gone = chaosBindingSeconds(); beats([[0.32, 'BINDS', true], [gone-2.4, 'circle fades'], [gone, 'released']]); }
  if (rejuvenating) { gone = rejuvenationLook.ranks[Number($('rejuvenationRank').value)-1].seconds; beats([[0.85, 'seal forms'], [gone-2.8, 'fades'], [gone, 'gone']]); }
  if (sundering) { const run = sunderRun(); gone = run.seconds; beats([[0, 'STRIKE', true], [1.6, 'fragments fall'], [run.duration, 'armor restored']]); }
  if (weakening) { const run = weakWillRun(); gone = run.seconds; beats([[.7, 'HELM FORMS', true], [3.25, 'lingering wisps'], [run.duration, 'release'], [gone, 'gone']]); }
  if (guarding) { const run = bastionRun(); gone = run.seconds; beats([[run.impact, 'SLAM', true], [run.stoneGone, 'ward remains'], [run.duration, 'release']]); }
  time = 0; crossed = false; stopUntil = 0; shakeUntil = 0;
  configureAudio();
  view('game');
}

const turned = new THREE.Quaternion();
let time = 0, playing = true, stopUntil = 0, shakeUntil = 0, crossed = false;
let last = performance.now();
const views = {
  bastion: {
    game: [[-350, 540, 320], [0, 0, 130]],
    side: [[650, 0, 275], [0, 0, 135]],
    front: [[0, 640, 260], [0, 0, 135]],
    close: [[-230, 355, 235], [0, 0, 130]],
    face: [[0, 1, 700], [0, 0, 0]]
  },
  weakwill: {
    game: [[-160, 285, 165], [0, 0, 103]],
    side: [[320, 20, 145], [0, 0, 108]],
    front: [[0, 310, 140], [0, 0, 106]],
    close: [[-95, 185, 158], [0, 0, 128]],
    face: [[0, 155, 140], [0, 0, 133]]
  },
  sunder: {
    game: [[-135, 240, 145], [0, 0, 75]],
    side: [[260, 40, 122], [0, 0, 76]],
    front: [[0, 280, 126], [0, 0, 75]],
    close: [[-70, 145, 115], [0, 0, 85]],
    face: [[0, 135, 110], [0, 0, 87]]
  },
  rejuvenation: {
    game: [[-650, -870, 580], [0, 0, 35]],
    side: [[1100, -100, 220], [0, 0, 50]],
    front: [[0, 1100, 420], [0, 0, 40]],
    close: [[-480, -560, 470], [0, 0, 30]],
    face: [[0, 0, 1200], [0, 1, 0]]
  },
  chaos: {
    game: [[-540, 680, 420], [0, 0, 185]],
    side: [[850, 0, 290], [0, 0, 200]],
    front: [[0, 850, 330], [0, 0, 200]],
    close: [[-330, 440, 350], [0, 0, 270]],
    face: [[0, 1, 1100], [0, 0, 0]]
  },
  warcry: {
    game: [[-400, -540, 300], [0, 0, 200]],
    side: [[650, 0, 270], [0, 0, 205]],
    front: [[0, 630, 270], [0, 0, 205]],
    close: [[-180, 350, 170], [0, 0, 108]],
    face: [[0, 390, 145], [0, 0, 108]]
  },
  taunt: {
    game: [[-350, -420, 230], [0, 60, 170]],
    side: [[650, 50, 245], [0, 60, 195]],
    front: [[40, 600, 230], [0, 70, 195]],
    close: [[-210, 370, 235], [0, 75, 210]],
    face: [[0, 430, 245], [0, 85, 215]]
  },
  wolf: {
    game: [[-300, -390, 210], [0, -20, 90]],  // over the fighter's shoulder
    side: [[520, 30, 120], [0, 0, 85]],
    front: [[30, 470, 130], [0, 0, 85]],
    close: [[-160, -150, 120], [0, 0, 90]],
    face: [[-170, 330, 220], [-20, -60, 150]]  // into its eyes, past the enemy
  },
  circle: {
    game: [[-560, -620, 330], [0, 0, 20]],  // over your shoulder, a few steps back
    side: [[760, 0, 160], [0, 0, 30]],
    front: [[0, 760, 200], [0, 0, 30]],
    close: [[-260, -240, 90], [80, 0, 30]],
    face: [[0, 0, 900], [0, 1, 0]]          // from above
  },
  blast: {
    game: [[-920, -860, 300], [0, 0, 70]],   // over the mage's shoulder
    side: [[820, -120, 170], [0, 0, 90]],
    front: [[80, 820, 210], [0, 0, 90]],
    close: [[-330, -260, 110], [0, 0, 80]],
    face: [[0, 0, 1300], [0, 1, 0]]          // from above: the reach against who stands where
  },
  magnus: {
    game: [[-420, -820, 300], [0, -50, 140]],  // a few steps behind and beside them, as you would stand
    side: [[560, -60, 180], [0, -60, 150]],
    front: [[90, 640, 190], [0, -60, 140]],    // the orb over their shoulder, pouring into them
    close: [[-170, -280, 210], [0, -100, 180]],
    face: [[0, -60, 900], [0, -59, 0]]
  },
  quake: {
    game: [[-520, -760, 330], [0, 0, 90]],   // over the bruiser's shoulder
    side: [[900, -100, 240], [0, 0, 140]],   // to see how high the third rank throws them
    front: [[60, 820, 260], [0, 0, 110]],
    close: [[-300, -330, 140], [40, 0, 80]],
    face: [[0, 0, 1400], [0, 1, 0]]
  },
  rain: {
    game: [[-1000, -1180, 360], [0, 0, 200]],  // over the archer's shoulder
    side: [[1300, -150, 420], [0, 0, 420]],    // to see the arrows come down from the sky
    front: [[100, 1150, 300], [0, 0, 250]],
    close: [[-420, -460, 170], [0, 0, 60]],
    face: [[0, -40, 950], [0, 0, 0]]           // from under the sky's glow, down onto the mark
  },
  renewal: {
    game: [[-430, -520, 230], [0, 0, 90]],     // a few steps back from the healer
    side: [[470, -30, 140], [0, 0, 90]],
    front: [[30, 470, 150], [0, 0, 90]],
    close: [[-150, -190, 150], [0, 0, 95]],    // the water running down them
    face: [[0, -20, 620], [0, 0, 0]]
  }
};
const chaosTargetViews = {
  game: [[-170, 245, 160], [0, 0, 55]],
  side: [[285, 30, 120], [0, 0, 55]],
  front: [[0, 295, 130], [0, 0, 55]],
  close: [[-95, 155, 100], [0, 0, 35]],
  face: [[0, 30, 260], [0, 0, 8]]
};
const warGrandViews = {
  game: [[-480, -720, 420], [0, 32, 300]],
  side: [[850, 32, 350], [0, 32, 300]],
  front: [[0, 800, 350], [0, 32, 300]],
  close: [[-340, 680, 350], [0, 32, 300]],
  face: [[0, 650, 320], [0, 32, 300]]
};
function view(name) {
  const collection = mode === 'warcry' && $('warCryRank').value === '3' ? warGrandViews : mode === 'chaos' && $('chaosView').value === 'target' ? chaosTargetViews : views[mode];
  const [eye, at] = collection[name];
  camera.position.copy(toThree(...eye));
  controls.target.copy(toThree(...at));
  document.querySelectorAll('[data-view]').forEach(b => b.classList.toggle('on', b.dataset.view === name));
}

// Magnus: frames taken as they come; rings and streams turned by the matrices the plugin uses.
const magnusRun = () => ($('magnusRank').value === 'grand' ? magnusLook.grand : magnusLook.plain);
const magnusBeats = () => { const run = magnusRun(); return [[magnusLook.grow, 'out of them'], [run.seconds - magnusLook.go, 'drawn back in', true], [run.seconds, 'gone']]; };
const setTurn = (group, m, s = 1, at = [0, 0, 0]) => {
  group.matrix.set(m[0] * s, m[1] * s, m[2] * s, at[0], m[3] * s, m[4] * s, m[5] * s, at[1], m[6] * s, m[7] * s, m[8] * s, at[2], 0, 0, 0, 1);
  group.matrixWorldNeedsUpdate = true;
};
function showMagnus(t) {
  const run = magnusRun();
  const f = run.frames[Math.min(Math.max(Math.round(t * magnusLook.fps), 0), run.frames.length - 1)];
  const g = magnus.groups, mat = magnus.materials;
  const set = (node, a, c) => { for (const m of mat.get(node) || []) { m.uniforms.opacity.value = a; m.uniforms.tint.value.set(c[0], c[1], c[2], 1); } };
  g.get('Orb').position.set(...f.orb);
  g.get('Orb').scale.setScalar(Math.max(f.scale, 0.0001));
  g.get('Shell').rotation.z = f.shell;
  g.get('Swirl').rotation.z = f.swirl;
  g.get('Rim').rotation.z = f.shell * -0.55;
  const c = f.colors;
  set('Core', f.alpha * (0.75 + 0.25 * f.core), c.core);
  set('Shell', f.alpha, c.shell);
  set('Swirl', f.alpha, c.swirl);
  set('Halo', f.halo, c.halo);
  set('Rim', f.rim, c.rim);
  f.rings.forEach((r, i) => { setTurn(g.get(`Ring${i}`), r); set(`Ring${i}`, f.alpha, c.ring); });
  f.tendrils.forEach((s, i) => { setTurn(g.get(`Tendril${i}`), s.turn, s.scale, s.from); set(`Tendril${i}`, s.alpha, c.stream); });
  f.veils.forEach((s, i) => { setTurn(g.get(`Veil${i}`), s.turn, s.scale, s.from); set(`Veil${i}`, s.alpha, c.stream); });
  f.wisps.forEach((w, i) => { const node = `Wisp${String(i).padStart(2, '0')}`; setTurn(g.get(node), w.turn, w.size, w.at); set(node, w.alpha, w.color); });
  f.flows.forEach(([x, y, z, size, alpha, r, gr, b], i) => {
    const node = `Flow${String(i).padStart(2, '0')}`;
    g.get(node).position.set(x, y, z);
    g.get(node).scale.setScalar(Math.max(size, 0.0001));
    set(node, alpha, [r, gr, b]);
  });
  for (const m of magnus.skins) m.uniforms.uvOffset.value.y = -t * magnusLook.drift;
  for (const m of magnus.streams) m.uniforms.uvOffset.value.y = -t * magnusLook.stream;
  magnusLight.position.set(...f.orb);
  magnusLight.color.setRGB(...c.light);
  magnusLight.intensity = f.light * magnusLook.lightFade * 60000;
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < magnusLook.grow ? 'coming out' : t < run.seconds - magnusLook.go ? 'pouring magicka' : 'drawn back in';
  $('line1').innerHTML = `size <b>${(f.scale * 100).toFixed(0)}</b>% &middot; flow <b>${(f.flow * 100).toFixed(0)}</b>%`;
  $('line2').innerHTML = `orb <b>${(magnusLook.radius * 2 * f.scale).toFixed(0)}</b> units across, ${Math.hypot(f.orb[0], f.orb[1]).toFixed(0)} behind`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

// Earthshaker: frames taken as they come. The fallen tip over where they stand and lie there; at rank
// III those near enough are thrown up (fifteen feet at the top), tumble down, and lie there longer.
const quakeRank = () => Number($('quakeRank').value);
const quakeRun = () => (quakeRank() === 3 ? quakeLook.grand : quakeLook.plain);
const quakeReach = () => [250, 300, 350][quakeRank() - 1];
const quakeLength = () => Math.max(quakeRun().seconds, (quakeRank() === 3 ? quakeLook.airborne : 0.4) + quakeLook.downed[quakeRank() - 1] + 1.2);
const quakeBeats = () => {
  const r = quakeRank(), down = (r === 3 ? quakeLook.airborne : 0.4);
  return [[0, 'SLAM', true], ...(r === 3 ? [[quakeLook.airborne / 2, 'top of the throw']] : []), [down, 'down'], [down + quakeLook.downed[r - 1], 'getting up']];
};
function showQuake(t) {
  const run = quakeRun(), rank = quakeRank(), reach = quakeReach();
  const f = run.frames[Math.min(Math.max(Math.round(t * quakeLook.fps), 0), run.frames.length - 1)];
  quake.groups.get('Quake').scale.setScalar(reach / quakeLook.meshRadius);
  quakeLook.parts.forEach((name, i) => {
    const p = f[i], g = quake.groups.get(name);
    setTurn(g, p.slice(3, 12), Math.max(p[12], 0.0001), p.slice(0, 3));
    for (const m of quake.materials.get(name) || []) if (m.uniforms) { m.uniforms.opacity.value = p[13]; m.uniforms.tint.value.set(p[14], p[15], p[16], 1); }
  });
  for (const m of quake.flowing) m.uniforms.uvOffset.value.y = -t * 0.2;
  const g = quakeLook.gravity, v = quakeLook.launchSpeed, air = quakeLook.airborne, held = quakeLook.downed[rank - 1];
  let thrown = 0, felled = 0;
  for (const foe of foes) {
    const [x, y] = foe.at, d = Math.hypot(x, y), out = [x / d, y / d];
    foe.body.rotation.set(Math.PI / 2, 0, 0);
    foe.body.position.set(x, y, 64);
    if (d > reach) continue;
    const fly = rank === 3 ? Math.min(t, air) : 0, landed = rank === 3 ? air : 0.4;
    const lying = Math.min(Math.max((t - (rank === 3 ? 0 : 0)) / 0.4, 0), 1) * (1 - Math.min(Math.max((t - landed - held) / 0.6, 0), 1));
    const z = rank === 3 ? Math.max(v * fly - 0.5 * g * fly * fly, 0) : 0;
    const drift = rank === 3 ? quakeLook.throwOut * fly : 0;
    foe.body.position.set(x + out[0] * drift, y + out[1] * drift, 20 + z + 44 * (1 - lying));
    // Tipped over away from the slam (and tumbling while in the air).
    const tip = (Math.PI / 2) * lying + (rank === 3 && t < air ? t * 4 : 0);
    // In the game's frame: upright, then turned about the level line square to the slam, top outward.
    foe.body.quaternion.setFromEuler(new THREE.Euler(Math.PI / 2, 0, 0)).premultiply(new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(-out[1], out[0], 0), tip));
    if (rank === 3) thrown++; else felled++;
  }
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < 0.15 ? 'SLAM' : rank === 3 && t < air ? 'thrown' : t < (rank === 3 ? air : 0.4) + held ? 'held down' : 'getting up';
  $('line1').innerHTML = `rank <b>${rank}</b> &middot; reach <b>${reach}</b> &middot; held down <b>${held}</b> s`;
  $('line2').innerHTML = rank === 3 ? `thrown: <b>${thrown}</b>, up to <b>${quakeLook.launchHeight.toFixed(0)}</b> units (15 ft)` : `felled: <b>${felled}</b>`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

// Arrow Rain: frames taken as they come. The trails are turned to this camera as the plugin turns them
// to the game's (facePart); the stand-in arrows fall where ArrowRain::DemoArrow puts them.
const rainRun = () => ($('rainRank').value === 'grand' ? rainLook.grand : rainLook.plain);
const rainBeats = () => { const run = rainRun(); return [[0, 'called'], [rainLook.call, 'BURSTS', true], [rainLook.call + 2, 'last arrow'], [run.seconds, 'gone']]; };
function showRain(t) {
  const run = rainRun(), g = rain.groups, mat = rain.materials;
  const f = run.frames[Math.min(Math.max(Math.round(t * rainLook.fps), 0), run.frames.length - 1)];
  const [mark, markScale, ring, dashes, heads, headsAlpha, outer, floor, sky, skyScale, swirl, crown, crownTurn, burst, burstScale] = f.look;
  const two = k => String(k).padStart(2, '0');
  const tint = (node, alpha, rgb) => paint(mat.get(node), alpha, rgb);
  const hide = (node, shown) => g.get(node).scale.setScalar(shown ? 1 : 0.0001);
  g.get('Mark').position.set(0, 0, 1);
  g.get('Mark').scale.setScalar(Math.max(markScale, 0.0001));
  g.get('Ring').rotation.z = ring;
  g.get('Dashes').rotation.z = dashes;
  g.get('Heads').rotation.z = ring;
  g.get('Heads').scale.setScalar(1 - rainLook.stroke * heads);
  g.get('Outer').rotation.z = -ring;
  tint('Ring', mark, rainLook.markColor);
  tint('Dashes', mark, rainLook.markColor);
  tint('Heads', headsAlpha, rainLook.markColor);
  tint('Outer', outer, rainLook.markColor);
  tint('Floor', floor, rainLook.markColor);
  g.get('Sky').position.set(0, 0, rainLook.sky);
  g.get('Sky').scale.setScalar(Math.max(skyScale, 0.0001));
  g.get('Swirl').rotation.z = swirl;
  g.get('Crown').rotation.z = crownTurn;
  tint('Halo', sky, rainLook.skyColor);
  tint('Swirl', sky, rainLook.skyColor);
  tint('Crown', crown, rainLook.skyColor);
  for (const m of rain.flowing) m.uniforms.uvOffset.value.y = -t * rainLook.drift;
  placePart(g.get('Flare'), [...f.flare.slice(0, 5), f.flare[5] > 0 ? 1 : 0]);
  tint('Flare', f.flare[5], rainLook.trailColor);
  g.get('Burst').position.set(0, 0, rainLook.sky);
  g.get('Burst').scale.setScalar(Math.max(burstScale, 0.0001));
  tint('Burst', burst, rainLook.trailColor);
  let falling = 0, struck = 0;
  for (let k = 0; k < 28; k++) {
    const a = f.arrows[k];
    const arrow = g.get(`Arrow${two(k)}`), hit = g.get(`Hit${two(k)}`);
    if (!a) { hide(`Arrow${two(k)}`, false); hide(`Hit${two(k)}`, false); continue; }
    const [x, y, z, yaw, tilt, length, alpha, head, body, sparkScale, spark, rippleScale, ripple, dustScale, dust, rise, hx, hy, hz] = a;
    arrow.position.set(x, y, z);
    facePart(arrow, [x, y, z, yaw, tilt], 1);
    hide(`Arrow${two(k)}`, body > 0 || alpha > 0 || head > 0);
    g.get(`Arrow${two(k)}Trail`).scale.setScalar(Math.max(length, 0.0001));
    tint(`Arrow${two(k)}Trail`, alpha, rainLook.trailColor);
    tint(`Arrow${two(k)}Shape`, body, rainLook.trailColor);
    tint(`Arrow${two(k)}Glint`, head, rainLook.trailColor);
    hit.position.set(hx, hy, hz + 2);
    hide(`Hit${two(k)}`, spark > 0 || ripple > 0 || dust > 0);
    g.get(`Hit${two(k)}Spark`).scale.setScalar(Math.max(sparkScale, 0.0001));
    g.get(`Hit${two(k)}Ripple`).scale.setScalar(Math.max(rippleScale, 0.0001));
    g.get(`Hit${two(k)}Dust`).position.set(0, 0, rise);
    g.get(`Hit${two(k)}Dust`).scale.setScalar(Math.max(dustScale, 0.0001));
    tint(`Hit${two(k)}Spark`, spark, rainLook.trailColor);
    tint(`Hit${two(k)}Ripple`, ripple, rainLook.markColor);
    tint(`Hit${two(k)}Dust`, dust, rainLook.dustColor);
    if (alpha > 0 && spark === 0 && dust === 0) falling++;
    if (spark > 0 || ripple > 0 || dust > 0) struck++;
  }
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < rainLook.call ? 'flare' : t < rainLook.call + 2.35 ? 'RAIN' : 'clearing';
  $('line1').innerHTML = `${run.arrows} arrows &middot; mark <b>${(mark * 100).toFixed(0)}</b>% &middot; reach <b>${rainLook.radius.toFixed(0)}</b>`;
  $('line2').innerHTML = `falling: <b>${falling}</b> &middot; striking: <b>${struck}</b>`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

// Renewal: frames taken as they come. The water drifts down each part as the plugin drifts it.
const cloakRun = () => ($('renewalRank').value === 'grand' ? cloakLook.grand : cloakLook.plain);
const cloakBeats = () => [[0, 'called'], [1, 'pulse'], [cloakLook.renewal - 0.8, 'falling away', true], [cloakLook.renewal, 'over']];
function showCloak(t) {
  const g = cloak.groups, mat = cloak.materials;
  const run = cloakRun(), water = run.water, foam = run.foam;
  const f = run.frames[Math.min(Math.max(Math.round(t * cloakLook.fps), 0), run.frames.length - 1)];
  const [veil, inner, streams, twist, crown, crownTurn, pool, poolScale, rise, light, gleam, gleamScale] = f.look;
  const tint = (node, alpha, rgb) => paint(mat.get(node), alpha, rgb);
  const drift = (node, rate) => { for (const m of mat.get(node) || []) m.uniforms.uvOffset.value.y = -t * rate; };
  const two = k => String(k).padStart(2, '0');
  g.get('Cloak').scale.setScalar(Math.max(rise * run.size * 128 / cloakLook.height, 0.0001));
  g.get('Gleam').scale.setScalar(Math.max(gleamScale, 0.0001));
  tint('GleamLift', gleam, foam);
  tint('Veil', veil, water);
  tint('Inner', inner, foam);
  g.get('Streams').rotation.z = twist;
  tint('Streams', streams, water);
  g.get('Crown').rotation.z = crownTurn;
  tint('Crown', crown, foam);
  tint('CrownLift', crown, foam);
  g.get('Pool').scale.setScalar(Math.max(poolScale, 0.0001));
  tint('Pool', pool, water);
  f.ripples.forEach(([scale, alpha], k) => { g.get(`Ripple${k}`).scale.setScalar(Math.max(scale, 0.0001)); tint(`Ripple${k}`, alpha, foam); });
  f.drops.forEach(([x, y, z, size, alpha], k) => { const d = g.get(`Drop${two(k)}`); d.position.set(x, y, z); d.scale.setScalar(Math.max(size, 0.0001)); tint(`Drop${two(k)}`, alpha, foam); });
  drift('Veil', cloakLook.flow);
  drift('Crown', cloakLook.flow);
  drift('Inner', cloakLook.flow * 0.6);
  drift('Streams', cloakLook.streamFlow);
  drift('Pool', 0.15);
  let washing = 0;
  f.washes.forEach(([ally, z, scale, alpha, ring, ringScale], k) => {
    const wash = g.get(`Wash${two(k)}`);
    if (ally < 0 || (alpha <= 0 && ring <= 0)) { wash.scale.setScalar(0.0001); return; }
    const at = cloakLook.allies[ally];
    wash.position.set(at[0], at[1], 0);
    wash.scale.setScalar(128 / cloakLook.height);
    g.get(`Wash${two(k)}Band`).position.set(0, 0, z);
    g.get(`Wash${two(k)}Band`).scale.setScalar(Math.max(scale, 0.0001));
    g.get(`Wash${two(k)}Ring`).scale.setScalar(Math.max(ringScale, 0.0001));
    tint(`Wash${two(k)}Band`, alpha, water);
    tint(`Wash${two(k)}Ring`, ring, foam);
    drift(`Wash${two(k)}Band`, cloakLook.flow * 2);
    if (alpha > 0) washing++;
  });
  cloakLight.position.set(0, 0, 90);
  cloakLight.color.setRGB(...water);
  cloakLight.intensity = light * cloakLook.lightFade * 60000;
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < 0.6 ? 'forming' : t < cloakLook.renewal - 0.8 ? 'flowing' : t < cloakLook.renewal ? 'falling away' : 'over';
  $('line1').innerHTML = `sheath <b>${(veil / 0.75 * 100).toFixed(0)}</b>% &middot; pool <b>${(pool * 100).toFixed(0)}</b>%`;
  $('line2').innerHTML = `washing: <b>${washing}</b> of the companions`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

function showBear(t, warcry = false) {
  const grand = warcry ? $('warCryRank').value === '3' : $('bearRank').value === 'grand';
  const frames = warcry ? (grand ? bearLook.warGrand : bearLook.warFrames) : (grand ? bearLook.grand : bearLook.frames);
  const f = frames[Math.max(0, Math.min(Math.round(t * bearLook.fps), frames.length - 1))];
  for (const [name, bear] of Object.entries(warcry ? warHeads : bears)) {
    bear.top.visible = name === (warcry ? (grand ? 'dragon' : 'sabre') : (grand ? ($('bearFur').checked ? 'red' : 'redPlain') : ($('bearFur').checked ? 'furred' : 'plain')));
    if (!bear.top.visible) continue;
    const c = Math.cos(f.yaw), s = Math.sin(f.yaw), cp = Math.cos(f.pitch), sp = Math.sin(f.pitch);
    setTurn(bear.groups.get('Head'), [c, s*cp, -s*sp, -s, c*cp, -c*sp, 0, sp, cp], f.s, [f.side, f.forward, f.height]);
    bear.groups.get('Jaw').rotation.x = -f.jaw;
    for (const name of ['Head', 'Jaw']) for (const m of bear.materials.get(name) || []) {
      m.uniforms.opacity.value = f.a; m.uniforms.tint.value.set(...f.tint, 1);
    }
    f.waves.forEach(([forward, radius, alpha], i) => {
      const node = bear.groups.get(`Wave${i}`);
      node.position.set(0, forward, warcry ? f.height : 205); node.scale.setScalar(radius);
      for (const m of bear.materials.get(`Wave${i}`) || []) {
        m.uniforms.tint.value.set(...f.glow, 1); m.uniforms.opacity.value = alpha;
      }
    });
  }
  $('phase').textContent = t < bearLook.roar ? 'appearing' : t < 2.95 ? 'roaring' : t < bearLook.fade ? 'settling' : 'fading';
  $('time').textContent = t.toFixed(2);
  $('line1').textContent = `Jaw ${(f.jaw * 180 / Math.PI).toFixed(0)}° · ${(f.a * 100).toFixed(0)}% visible`;
  $('line2').textContent = warcry ? `${grand ? 'Rank III · larger crimson sabre-cat' : 'Rank I–II · golden sabre-cat'} · close to the follower · slight forward offset` : `${grand ? 'Rank III · 45% larger red bear' : 'Rank I-II · amber bear'} · distance-based shake in Skyrim`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

const chaosBindingSeconds = () => 8 + Number($('chaosLingering').value);
function showChaosBinding(t, grand) {
  const focused = $('chaosView').value === 'target', duration = chaosBindingSeconds();
  const run = chaosLook.binding.runs?.[duration] || chaosLook.binding;
  const frames = grand ? run.grand : run.frames;
  const frame = frames[Math.min(Math.max(Math.round(t * chaosLook.fps), 0), frames.length - 1)];
  for (const [i, affected] of chaosAffected.entries()) {
    const visible = !focused || i === 0;
    affected.body.visible = affected.binding.top.visible = visible;
    if (!visible) continue;
    const at = focused ? [0, 0] : affected.at;
    affected.body.position.set(at[0], at[1], 64);
    affected.binding.top.position.set(at[0], at[1], 0);
    affected.binding.top.scale.setScalar(grand ? 1.08 : 1);
    chaosLook.binding.names.forEach((name, index) => {
      const node = affected.binding.groups.get(name), p = frame.parts[index];
      node.position.set(0, 0, p.z); node.rotation.z = p.r; node.scale.setScalar(Math.max(p.s, 0.0001));
      for (const m of affected.binding.materials.get(name) || []) {
        m.uniforms.opacity.value = p.a; m.uniforms.tint.value.set(...p.c, 1);
        if (index >= 1 && index <= 3) m.uniforms.uvOffset.value.y = -t * (index === 1 ? 0.13 : 0.23);
      }
    });
  }
}
function showChaos(t) {
  const grand = $('chaosRank').value === '55';
  const focused = $('chaosView').value === 'target';
  showChaosBinding(t, grand);
  const chaos = grand ? chaosModels.grand : chaosModels.plain;
  chaosModels.plain.top.visible = !focused && !grand; chaosModels.grand.top.visible = !focused && grand;
  const reach = chaosLook.radius + Number($('chaosWide').value);
  const scale = reach / chaosLook.radius;
  const run=chaosLook.runs?.[chaosBindingSeconds()]||chaosLook;
  const frames = grand ? run.grand : run.frames;
  const f = frames[Math.min(Math.max(Math.round(t * chaosLook.fps), 0), frames.length - 1)];
  chaosLook.names.forEach((name, i) => {
    const node = chaos.groups.get(name), p = f.parts[i];
    node.position.set(0, 0, 3 + p.z); node.rotation.z = p.r; node.scale.setScalar(p.s * scale);
    for (const m of chaos.materials.get(name) || []) {
      m.uniforms.tint.value.set(...p.c, 1); m.uniforms.opacity.value = p.a;
    }
  });
  const spirit = chaos.groups.get('Spirit'), s = f.spirit;
  spirit.position.set(0,s.y,s.z); spirit.scale.setScalar(s.s); spirit.rotation.set(s.lean,s.roll,s.twist,'ZYX');
  chaosLook.joints.forEach((name,i) => {
    const node=chaos.groups.get(name), b=s.bones[i]; node.matrixAutoUpdate=false;
    setTurn(node,b.m,1,b.at);
  });
  for (const name of ['Spirit', ...chaosLook.joints]) for (const m of chaos.materials.get(name) || []) {
    m.uniforms.tint.value.set(...s.c, 1); m.uniforms.opacity.value = s.a;
  }
  for (const m of chaos.flowing) m.uniforms.uvOffset.value.y = -t * 0.15;
  blastLight.position.set(0, 0, 40); blastLight.color.setRGB(...f.parts[0].c);
  blastLight.intensity = focused ? 0 : f.light * 60000; blastLight.distance = 280 * scale;
  $('phase').textContent = t >= gone ? 'released' : focused || t >= chaosLook.seconds ? 'restrained' : t < 0.7 ? 'appearing' : t < 2.25 ? 'reaching over' : t < 3.2 ? 'drawing inward' : 'circle fading';
  $('time').textContent = t.toFixed(2);
  $('line1').textContent = `${$('chaosRank').value}% slower movement and attacks · ${8 + Number($('chaosLingering').value)} seconds`;
  $('line2').textContent = focused ? `${grand ? 'Deep purple' : 'Violet'} restraint · ${(Math.max(0, gone - t)).toFixed(1)} s remaining` : `${grand ? 'Crowned draugr' : 'Spectral skeleton'} · radius ${reach} · 3 enemies restrained`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

function showRejuvenation(t) {
  const rank = Number($('rejuvenationRank').value), run = rejuvenationLook.ranks[rank - 1];
  const f = run.frames[Math.min(Math.max(Math.round(t * rejuvenationLook.fps), 0), run.frames.length - 1)];
  const scale = run.radius / 280, g = rejuvenation.groups;
  const set = (node, alpha, color) => {
    for (const m of rejuvenation.materials.get(node) || []) {
      m.uniforms.opacity.value = alpha;
      m.uniforms.tint.value.set(...color, 1);
    }
  };
  rejuvenationLook.names.forEach((name, i) => {
    const [size, turn, height, alpha, r, green, b] = f.parts[i], node = g.get(name);
    node.position.set(0, 0, 8 + height);
    node.rotation.z = turn;
    node.scale.setScalar(scale * size);
    set(name, alpha, [r, green, b]);
  });
  f.motes.forEach(([x, y, z, size, alpha, r, green, b], i) => {
    const name = `Mote${String(i).padStart(2, '0')}`, node = g.get(name);
    node.position.set(x * scale, y * scale, z * scale);
    node.scale.setScalar(size * scale);
    set(name, alpha, [r, green, b]);
  });
  for (const m of rejuvenation.flowing) m.uniforms.uvOffset.value.y = -t * 0.14;
  blastLight.position.set(0, 0, 50); blastLight.color.setRGB(...f.lightColor);
  blastLight.intensity = f.light * 60000; blastLight.distance = run.radius * 1.5;
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < 0.85 ? 'seal forming' : t < run.seconds-2.8 ? 'rejuvenating' : 'fading';
  $('line1').textContent = `Rejuvenation ${['I', 'II', 'III'][rank - 1]} · Azure seal · white heart`;
  $('line2').textContent = rank === 3 ? 'Grand circle · floating pulses and rich motes' : 'Floating edge pulses · rising motes';
  $('scrub').value = String(Math.round(t / gone * 1000));
}

const sunderRun = () => ($('sunderRank').value === 'grand' ? sunderLook.grand : sunderLook.plain);
function showSunder(t) {
  const grand = $('sunderRank').value === 'grand', run = sunderRun();
  const f=run.frames[Math.min(Math.max(Math.round(t*sunderLook.fps),0),run.frames.length-1)];
  sunderLook.parts.forEach((name,i) => {
    const p=f.parts[i],node=sunder.groups.get(name);
    node.position.set(p[0],p[1],p[2]); node.rotation.set(p[5],p[4],p[3],'ZYX');
    node.scale.setScalar(Math.max(p[6],0.0001));
    for(const m of sunder.materials.get(name)||[]) { m.uniforms.opacity.value=p[7];m.uniforms.tint.value.set(p[8],p[9],p[10],1); }
  });
  for(const m of sunder.flowing) m.uniforms.uvOffset.value.y=t*0.09;
  blastLight.position.set(0,45,sunderTarget.chestHeight); blastLight.color.setRGB(grand ? 1 : 0.95,grand ? 0.3 : 0.55,0.13);
  blastLight.intensity=f.light*14000; blastLight.distance=240;
  $('time').textContent=t.toFixed(2);
  $('phase').textContent=t<0.25 ? 'armor cracking' : t<1.6 ? 'armor rupture' : t<run.duration ? 'guard broken' : 'fading';
  $('line1').textContent=grand ? 'Sunder III · heavy fracture and more flying metal' : 'Sunder I-II · fractured armor and metal fragments';
  $('line2').textContent=t<run.duration ? 'Fractured mark remains while armor is weakened' : 'Armor returns to normal';
  $('scrub').value=String(Math.round(t/gone*1000));
}

const weakWillRun = () => {
  const rank = Number($('weakWillRank').value), seconds = 6 + rank * 2 + Number($('weakWillDuration').value);
  return weakWillLook.runs[String(seconds)] || weakWillLook;
};
function showWeakWill(t) {
  const rank = Number($('weakWillRank').value), run = weakWillRun();
  const model = weakWillModels[rank >= 3 ? 'grand' : 'plain'];
  for (const other of Object.values(weakWillModels)) other.top.visible = other === model;
  const frames = run.ranks[rank - 1], frame = frames[Math.min(Math.max(Math.round(t * run.fps), 0), frames.length - 1)];
  run.names.forEach((name, i) => {
    const p = frame.parts[i], node = model.groups.get(name);
    node.position.set(0, p[2], 120 + p[1]); node.rotation.set(p[4], 0, p[3], 'ZYX'); node.scale.setScalar(p[0]);
    for (const material of model.ownedMaterials.get(name)) {
      material.uniforms.opacity.value = p[5]; material.uniforms.tint.value.set(p[6], p[7], p[8], 1);
      if (name === 'Mist') material.uniforms.uvOffset.value.y = -.13 * t;
    }
  });
  blastLight.position.set(0, 0, 127); blastLight.color.setRGB(...(rank >= 3 ? [.57,.09,.90] : [.4,.65,.92]));
  blastLight.intensity = frame.light * 16000; blastLight.distance = 170;
  $('time').textContent = t.toFixed(2);
  $('phase').textContent = t < .7 ? 'helm forming' : t < 1.35 ? 'weak will invoked' : t < 3.25 ? 'helmet dissolving' : t < run.duration ? 'lingering wisps' : 'releasing';
  $('line1').textContent = `Weak Will ${['I', 'II', 'III'][rank - 1]} · ${rank >= 3 ? 'purple ebony helmet' : 'spectral steel helmet'}`;
  $('line2').textContent = t < 3.25 ? 'Fades in · slowly dissolves into wisps' : `Caster and attackers deal ${[20,30,40][rank-1]}% less damage · quiet shoulder aura`;
  $('scrub').value = String(Math.round(t / gone * 1000));
}

const bastionRun = () => bastionLook.runs[$('bastionDuration').value] || bastionLook;
function showBastion(t) {
  const rank=Number($('bastionRank').value),run=bastionRun(),model=bastionModels[rank>=3?'grand':'plain'];
  for(const other of Object.values(bastionModels))other.top.visible=other===model;
  const frames=run.ranks[rank-1],f=frames[Math.min(Math.max(Math.round(t*run.fps),0),frames.length-1)];
  const move=$('bastionMove').checked ? Math.min(Math.max(t-2,0)*25,150) : 0;
  weakWillTank.body.position.x=move;
  run.names.forEach((name,i)=>{
    const p=f.parts[i],n=model.groups.get(name);n.position.set(i>=4?move:0,0,p[1]);n.rotation.z=p[2];n.scale.setScalar(p[0]);
    for(const m of model.ownedMaterials.get(name)){m.uniforms.opacity.value=p[3];m.uniforms.tint.value.set(p[4],p[5],p[6],1);if(i===3||i===4)m.uniforms.uvOffset.value.y=-.10*t;}
  });
  blastLight.position.set(move,0,55);blastLight.color.setRGB(...f.parts[1].slice(4));blastLight.intensity=f.light*16000;blastLight.distance=210;
  $('time').textContent=t.toFixed(2);
  $('phase').textContent=t<.62?'fortress forming':t<run.impact?'descending':t<1.55?'ground impact':t<run.stoneGone?'stone dissolving':t<run.duration?'guardian ward':'releasing';
  $('line1').textContent='Last Bastion '+['I','II','III'][rank-1]+(rank>=3?' · Crimson fortress':' · Spectral watchtower');
  $('line2').textContent=t<run.stoneGone?(rank>=3?'Deep summon · fortress crash · low rumble':'Ward shimmer · stone landing · falling rubble'):'Protective ward follows the tank';
  $('scrub').value=String(Math.round(t/gone*1000));
}
function show(t) {
  if (mode === 'bastion') { showBastion(t); return; }
  if (mode === 'weakwill') { showWeakWill(t); return; }
  if (mode === 'sunder') { showSunder(t); return; }
  if (mode === 'rejuvenation') { showRejuvenation(t); return; }
  if (mode === 'chaos') { showChaos(t); return; }
  if (mode === 'taunt' || mode === 'warcry') { showBear(t, mode === 'warcry'); return; }
  if (mode === 'renewal') {
    showCloak(t);
    return;
  }
  if (mode === 'rain') {
    showRain(t);
    return;
  }
  if (mode === 'quake') {
    showQuake(t);
    return;
  }
  if (mode === 'magnus') {
    showMagnus(t);
    return;
  }
  if (mode === 'blast') {
    showBlast(t);
    return;
  }
  if (mode === 'circle') {
    showCircle(t);
    return;
  }
  const f = frameAt(t);
  const grand = $('wolfRank').value === 'grand';
  const which = grand ? ($('fur').checked ? 'redFurred' : 'redPlain') : ($('fur').checked ? 'furred' : 'plain');
  for (const [name, h] of Object.entries(heads)) {
    h.head.visible = name === which;
    if (!h.head.visible) continue;
    h.head.matrix.set(f.r[0] * f.s, f.r[1] * f.s, f.r[2] * f.s, f.p[0], f.r[3] * f.s, f.r[4] * f.s, f.r[5] * f.s, f.p[1], f.r[6] * f.s, f.r[7] * f.s, f.r[8] * f.s, f.p[2], 0, 0, 0, 1);
    h.head.matrixWorldNeedsUpdate = true;
    h.jaw.rotation.x = -f.jaw;
    for (const m of h.materials) {
      if (m.uniforms) m.uniforms.opacity.value = f.a; else m.opacity = f.a;
    }
  }
  const caps = $('capsules').checked;
  headCapsule.visible = bodyCapsule.visible = caps;
  if (caps) { const hit={...f,...f.hit};stretch(headCapsule, place(hit, pose.points.back), place(hit, pose.points.front), pose.radius * hit.s); }
  const flash = t >= pose.beats.snap && t < pose.beats.snap + 0.25 && biteLands;
  enemy.material.color.set(flash ? 0xb02a20 : 0x9a948a);
  // Caught still mid-stagger, in the ghostly smoke.
  tipEnemy(biteLands ? f.recoil : 0);
  const [mist, glow, ground, light, size, wisps] = f.haunt, g = haunt.groups, colour = pose.haunt.glow[grand ? 1 : 0], smoke = pose.haunt.smoke[grand ? 1 : 0];
  const on = biteLands ? 1 : 0;
  haunt.top.visible = true;
  g.get('Haunt0').scale.setScalar(Math.max(size * enemyHeight / pose.haunt.height, 0.0001));
  paint(haunt.materials.get('Haunt0Mist'), mist * on, smoke);
  paint(haunt.materials.get('Haunt0Glow'), glow * on, colour);
  paint(haunt.materials.get('Haunt0Ground'), ground * on, smoke);
  paint(haunt.materials.get('Haunt0Ring'), ground * 0.8 * on, colour);
  paint(haunt.materials.get('Haunt0FlareLift'), light * on, colour);
  wisps.forEach(([x, y, z, scale, alpha], w) => { const p = g.get(`Haunt0Wisp${w}`); p.position.set(x, y, z); p.scale.setScalar(Math.max(scale, 0.0001)); paint(haunt.materials.get(`Haunt0Wisp${w}`), alpha * on, colour); });
  const since = Math.max(t - pose.beats.snap, 0);
  for (const [node, rate] of [['Haunt0Mist', 1], ['Haunt0Glow', 1.4], ['Haunt0Ground', 0.3]]) for (const m of haunt.materials.get(node) || []) m.uniforms.uvOffset.value.y = since * pose.haunt.rise * rate;
  $('time').textContent = t.toFixed(2);
  $('line1').innerHTML = `jaw <b>${(f.jaw * 180 / Math.PI).toFixed(0)}</b>&deg; &middot; fade <b>${(f.a * 100).toFixed(0)}</b>%`;
  $('line2').innerHTML = `head <b>${(24 * f.s).toFixed(0)}</b> units long`;
  $('phase').textContent = phaseOf(t);
  $('scrub').value = String(Math.round(t / gone * 1000));
}

function tick(now) {
  const real = Math.max(0, Math.min((now - last) / 1000, 0.1));
  last = now;
  if (playing) {
    // The game's hit-stop: the world runs at a fraction of its speed for a blink of real time.
    const slow = $('hitstop').checked && now < stopUntil ? pose.beats.hitStopSpeed : 1;
    const before = time;
    time += real * Number($('speed').value) * slow;
    if ((mode === 'taunt' || mode === 'warcry') && before < bearLook.roar && time >= bearLook.roar) {
      shakeUntil = now + 900 / Number($('speed').value);
    }
    if (mode === 'wolf' && !crossed && before < pose.beats.snap && time >= pose.beats.snap) {
      crossed = true;
      stopUntil = now + pose.beats.hitStop * 1000 / Number($('speed').value);
      shakeUntil = now + 600 / Number($('speed').value);
    }
    if (time >= gone) {
      if ($('loop').checked) { time = 0; crossed = false; soundPlayer.reset(); } else { time = gone; playing = false; $('play').textContent = 'Play'; }
    }
  }
  show(time);
  soundPlayer.update(time,playing,Number($('speed').value));
  controls.update();
  // Billboards: square to the camera whatever their parents' turn, as the game's rigid face-camera mode.
  for (const g of billboards) {
    if (!g.parent) continue;
    g.parent.getWorldQuaternion(turned);
    g.quaternion.copy(turned.invert().multiply(camera.quaternion));
  }
  const shaking = $('shake').checked && now < shakeUntil;
  const jolt = shaking ? (shakeUntil - now) / 600 * 6 : 0;
  // A brief damped jolt at the shared impact time; pausing freezes the view.
  const bastionAge = time - bastionLook.impact;
  const bastionJolt = mode === 'bastion' && playing && $('shake').checked && bastionAge >= 0 && bastionAge < bastionLook.shake.seconds
    ? 14 * bastionLook.shake.strength[Number($('bastionRank').value)-1] * Math.pow(1-bastionAge/bastionLook.shake.seconds,2) : 0;
  const offsetX = (Math.random() - 0.5) * jolt + Math.sin(bastionAge*85)*bastionJolt;
  const offsetY = (Math.random() - 0.5) * jolt + Math.cos(bastionAge*73)*bastionJolt*.45;
  const offsetZ = Math.sin(bastionAge*68)*bastionJolt*.55;
  camera.position.x += offsetX;
  camera.position.y += offsetY;
  camera.position.z += offsetZ;
  renderer.render(scene, camera);
  camera.position.x -= offsetX;
  camera.position.y -= offsetY;
  camera.position.z -= offsetZ;
  requestAnimationFrame(tick);
}

function resize() {
  const w = window.innerWidth, h = window.innerHeight;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}

// --- Controls --------------------------------------------------------------------------------

$('play').onclick = () => { playing = !playing; $('play').textContent = playing ? 'Pause' : 'Play'; last = performance.now(); soundPlayer.unlock(); soundPlayer.update(time,playing,Number($('speed').value)); };
$('restart').onclick = () => { soundPlayer.reset(); shakeUntil=stopUntil=0; time = 0; crossed = false; playing = true; $('play').textContent = 'Pause'; };
$('scrub').oninput = e => { soundPlayer.reset(); shakeUntil=stopUntil=0; playing = false; $('play').textContent = 'Play'; time = Number(e.target.value) / 1000 * gone; crossed = time >= pose.beats.snap; };
document.querySelectorAll('[data-view]').forEach(b => { b.onclick = () => view(b.dataset.view); });
$('enemy').onchange = e => { const [h, r] = e.target.value.split(',').map(Number); setEnemy(h, r); };
$('light').onchange = e => setLight(e.target.value);
$('scene').onchange = e => e.target.value === 'surge' ? location.assign('surge.html') : e.target.value === 'windstep' ? location.assign('windstep.html') : e.target.value === 'dragonbreath' ? location.assign('dragon-breath.html') : e.target.value === 'berserk' ? location.assign('berserk.html') : ['volley','explosive'].includes(e.target.value) ? location.assign('arrowarts.html?ability='+e.target.value) : e.target.value === 'riposte' ? location.assign('riposte.html') : e.target.value === 'disarm' ? location.assign('disarm.html') : e.target.value === 'azura' ? location.assign('azura.html') : e.target.value === 'secondwind' ? location.assign('secondwind.html') : setScene(e.target.value);
$('element').onchange = () => setScene('blast');
$('magnusRank').onchange = () => setScene('magnus');
$('rejuvenationRank').onchange = () => setScene('rejuvenation');
$('healRank').onchange = () => setScene('circle');
$('reach').onchange = () => setScene('circle');
$('quakeRank').onchange = () => setScene('quake');
$('sunderRank').onchange = () => setScene('sunder');
$('weakWillRank').onchange = () => setScene('weakwill');
$('weakWillDuration').onchange = () => setScene('weakwill');
$('bastionRank').onchange = () => setScene('bastion');
$('bastionDuration').onchange = () => setScene('bastion');
$('chaosView').onchange = () => setScene('chaos');
$('chaosRank').onchange = () => setScene('chaos');
$('chaosLingering').onchange = () => setScene('chaos');
$('rainRank').onchange = () => setScene('rain');
$('wolfRank').onchange = () => { setScene('wolf'); judge(); };
$('warCryRank').onchange = () => setScene('warcry');
$('bearRank').onchange = () => setScene('taunt');
$('renewalRank').onchange = () => setScene('renewal');
$('tier').onchange = () => setScene('blast');
// The tiers' reaches as the plugin has them.
[...$('tier').options].forEach((o, i) => { o.textContent = `${['Apprentice', 'Adept', 'Expert'][i]} (reach ${blastLook.radius[i]}${i === blastLook.grandTier ? ', grand' : ''})`; });
window.addEventListener('keydown', e => { if (e.code === 'Space') { e.preventDefault(); $('play').click(); } });
window.addEventListener('resize', resize);

// For stepping through it from the console: preview.seek(1.05), preview.view('side').
window.preview = {
  seek(t) { playing = false; $('play').textContent = 'Play'; time = Math.min(Math.max(t, 0), gone); crossed = time >= pose.beats.snap; },
  view,
  scene(name) { $('scene').value = name; setScene(name); },
  element(name) { $('element').value = name; setScene('blast'); },
  tier(n) { $('tier').value = String(n); setScene('blast'); },
  play() { playing = true; $('play').textContent = 'Pause'; last = performance.now(); }
};

setLight('day');
setEnemy(128, 20);
const requestedScene = new URLSearchParams(location.search).get('scene');
if(requestedScene==='berserk')location.replace('berserk.html'+(location.search.includes('rank=3')?'?rank=3':''));
const requestedElement = new URLSearchParams(location.search).get('element');
const requestedRank = new URLSearchParams(location.search).get('rank');
if (requestedScene === 'warcry' && ['1','2','3'].includes(requestedRank)) $('warCryRank').value = requestedRank;
if (['fire','frost','shock'].includes(requestedElement)) $('element').value = requestedElement;
setScene(Object.hasOwn(views, requestedScene) ? requestedScene : 'wolf');
$('scene').value = mode;
resize();
status.remove();
requestAnimationFrame(tick);

if(requestedScene==='riposte'||requestedScene==='disarm'||requestedScene==='secondwind'||requestedScene==='azura')location.replace(requestedScene+'.html'+(location.search.includes('rank=3')?'?rank=3':''));

if(requestedScene==='volley'||requestedScene==='explosive')location.replace('arrowarts.html?ability='+requestedScene+'&rank='+(new URLSearchParams(location.search).get('rank')||1));

if(new URLSearchParams(location.search).get("scene")==="windstep") location.replace("windstep.html?rank="+(new URLSearchParams(location.search).get("rank")||1));

if(new URLSearchParams(location.search).get("scene")==="surge")location.replace("surge.html?rank="+(new URLSearchParams(location.search).get("rank")||1));
