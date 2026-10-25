import * as THREE from 'three';
import {SoundPlayer} from './sound-player.js';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
const $=id=>document.getElementById(id), scene=new THREE.Scene();scene.background=new THREE.Color('#10151e');
const camera=new THREE.PerspectiveCamera(43,innerWidth/innerHeight,1,7000);camera.up.set(0,0,1);camera.position.set(1900,1750,1100);
const renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));renderer.setSize(innerWidth,innerHeight);document.body.append(renderer.domElement);
const orbit=new OrbitControls(camera,renderer.domElement);orbit.target.set(0,700,400);orbit.update();
scene.add(new THREE.HemisphereLight(0xd6e3ff,0x303449,2));const grid=new THREE.GridHelper(2600,52,0x39455c,0x222e40);grid.rotation.x=Math.PI/2;scene.add(grid);
const body=new THREE.Mesh(new THREE.CapsuleGeometry(15,70,6,12),new THREE.MeshStandardMaterial({color:0x8b94a6,transparent:true,opacity:.55}));body.rotation.x=Math.PI/2;body.position.z=75;scene.add(body);
const skull=new THREE.Mesh(new THREE.SphereGeometry(13,16,12),body.material);skull.position.z=137;scene.add(skull);
for(const x of [-10,10]){const leg=new THREE.Mesh(new THREE.CylinderGeometry(7,6,45,8),body.material);leg.rotation.x=Math.PI/2;leg.position.set(x,0,24);scene.add(leg);}
const loader=new THREE.TextureLoader(),textures=new Map(),models={};
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


async function load(name){
 const data=await fetch('out/'+name+'.json').then(r=>{if(!r.ok)throw Error(name+' failed to load');return r.json();}),top=new THREE.Group(),nodes={},facing=[];
 for(const n of data.nodes){const node=new THREE.Group();node.position.fromArray(n.translate);node.userData.rest=node.position.clone();nodes[n.name]=node;if(n.billboard)facing.push(node);}
 for(const n of data.nodes)(nodes[n.parent]||top).add(nodes[n.name]);
 for(const s of data.shapes){const mesh=new THREE.Mesh(geometryOf(s),glowMaterial(s));const tr=s.transform;mesh.position.fromArray(tr.translate);mesh.scale.setScalar(tr.scale);mesh.setRotationFromMatrix(new THREE.Matrix4().set(...[...tr.rotate.slice(0,3),0,...tr.rotate.slice(3,6),0,...tr.rotate.slice(6,9),0,0,0,0,1]));nodes[s.node].add(mesh);}
 top.visible=false;scene.add(top);return {top,nodes,facing};
}
const look=await fetch('out/breath-look.json').then(r=>r.json());
for(const name of ['head','alduin',...['fire','frost','shock'].flatMap(e=>[1,2,3].map(r=>e+'-'+r))])models[name]=await load('breath-'+name);
const audioData=await fetch('out/audio-plans.json').then(r=>r.json()),sound=new SoundPlayer(audioData,$('audio'),$('sound'),$('soundStatus'));
function audioPlan(){const rank=Number($('rank').value),element=Number($('element').value),run=look.ranks[rank-1],cues=audioData.plans.dragonbreath[rank-1].map(c=>({...c,at:c.at+look.release,form:c.channel?audioData.existing[['breathFire','breathFrost','breathShock'][element]]:c.form}));sound.load(cues,look.release+run.duration+.25);}

let age=0,paused=false,last=performance.now(),close=false;
$('view').onclick=()=>{close=!close;camera.position.set(...(close?[600,1000,390]:[1900,1750,1100]));orbit.target.set(...(close?[0,340,250]:[0,700,400]));orbit.update();$('view').textContent=close?'Stream view':'Head view';};
$('angle').onchange=()=>{const mode=$('angle').value,poses={overview:[1900,1750,1100],side:[1900,650,400],front:[0,2300,320],rear:[0,-1500,350]};camera.position.set(...poses[mode]);orbit.target.set(0,650,mode==='overview'?400:280);orbit.update();};
const params=new URLSearchParams(location.search);$('rank').value=['1','2','3'].includes(params.get('rank'))?params.get('rank'):'1';$('element').value=String(Math.max(0,['fire','frost','shock'].indexOf(params.get('element'))));
audioPlan();
const restart=()=>{sound.unlock();audioPlan();age=0;paused=false;$('pause').textContent='Pause';};$('rank').onchange=restart;$('element').onchange=restart;$('replay').onclick=restart;
$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Play':'Pause';};$('timeline').oninput=()=>{age=Number($('timeline').value)/1000*look.ranks[Number($('rank').value)-1].seconds;paused=true;$('pause').textContent='Play';};
function paint(node,color,alpha,blended=false,drift=0){node?.traverse(o=>{if(o.material?.uniforms){const u=o.material.uniforms;u.tint.value.set(color[0]*(blended?1:alpha),color[1]*(blended?1:alpha),color[2]*(blended?1:alpha),blended?alpha:1);if(/fxfirescrolltilegrey|cloudtilefrostsmall|cloudtile.dds|darkswirls|fxembers/i.test(o.material.userData.source||''))u.uvOffset.value.y=drift;}});}
// Keep the same drifting-texture selection as the native material animator.
for(const name of Object.keys(models)){const data=await fetch('out/breath-'+name+'.json').then(r=>r.json());let i=0;models[name].top.traverse(o=>{if(o.material)o.material.userData.source=data.shapes[i++].source;});}
const ease=t=>{t=Math.max(0,Math.min(1,t));return t*t*(3-2*t);},profile=f=>.16+.84*ease(f/.12);
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;const rank=Number($('rank').value),element=Number($('element').value),run=look.ranks[rank-1];if(!paused){const before=age;age=(age+dt)%(run.seconds+1);if(age<before)sound.reset();}sound.update(age,!paused,1);
 const p=run.frames[Math.min(run.frames.length-1,Math.floor(age*60))],head=models[rank===3?'alduin':'head'],flow=models[['fire','frost','shock'][element]+'-'+rank];
 for(const m of Object.values(models))m.top.visible=false;head.top.visible=flow.top.visible=true;
 head.nodes.Head.position.set(0,p.forward,p.height);head.nodes.Head.scale.setScalar(p.s);head.nodes.Head.rotation.x=p.pitch;head.nodes.Jaw.rotation.x=p.jaw;
 const rune=head.nodes.SummoningRune;rune.position.set(0,15,p.runeRadius+12);rune.scale.setScalar(p.runeRadius);rune.rotation.y=p.runeAngle;paint(rune,[1,1,1],p.runeAlpha,true);
 paint(head.nodes.Head,rank===3?[.9,.13,.1]:[.65,.85,1],p.a,true);
 const mouth=new THREE.Vector3(0,p.forward+p.muzzleY*p.s,p.height+p.muzzleZ*p.s),direction=new THREE.Vector3(0,1,0),right=new THREE.Vector3().crossVectors(direction,new THREE.Vector3(0,0,1)).normalize(),up=new THREE.Vector3().crossVectors(right,direction);
 flow.nodes.Stream.position.copy(mouth);flow.nodes.Stream.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(right,direction,up));flow.nodes.Stream.scale.setScalar(Math.max(run.reach*p.front/1000,.001));
 const color=[[1,.28,.035],[.32,.72,1],[.45,.42,1]][element];
 for(let i=0;i<8;i++){const flicker=element===2?.35+.65*Math.abs(Math.sin(age*39+i*1.7)):.8+.2*Math.sin(age*11+i);flow.nodes['Jet'+i].rotation.y=.10*Math.sin(age*6-i*.7);paint(flow.nodes['Jet'+i],rank===3&&element===0&&i<3?[.06,.40,1]:rank===3&&element===1?[.58,.88,1]:color,p.stream*flicker,false,-age*(element===0?3.5:1.5)+i*.17);}
 for(let i=0;i<32;i++){const f=(age*(element===2?2.1:1.25)+i/32)%1,a=i*2.399+age,r=run.radius*1000/run.reach*profile(f)*(rank===3&&element===0?.78+.12*(i%7)/6:.15+.55*(i%7)/6),n=flow.nodes['Puff'+i];n.position.set(Math.cos(a)*r,1000*f,Math.sin(a)*r);n.scale.setScalar(.7+.5*Math.sin(f*Math.PI));paint(n,rank===3&&element===1?[.58,.88,1]:color,p.stream*Math.sin(f*Math.PI)*(rank===3?1:.65),false,-age*.35);}
 if(element===2)for(let i=0;i<64;i++){const [angle,x,z,alpha]=p.arcs[i],n=flow.nodes['Arc'+i];n.rotation.y=angle;n.position.copy(n.userData.rest).add(new THREE.Vector3(x,0,z));paint(n,color,p.stream*alpha);}
 if(rank===3&&element===1)for(let i=0;i<16;i++) {const n=flow.nodes['Ice'+i],f=(age*1.65+i/16)%1,a=i*2.399+age*2,r=run.radius*1000/run.reach*profile(f)*.72;n.position.set(Math.cos(a)*r,1000*f,Math.sin(a)*r);paint(n,[.65,.9,1],p.stream*Math.sin(f*Math.PI));}
 if(rank===3&&element===2)for(let i=0;i<4;i++) {const n=flow.nodes['Ground'+i],[alpha,fraction,side]=p.ground[i],from=mouth.clone().addScaledVector(direction,run.reach*fraction),to=from.clone().addScaledVector(right,run.radius*side);to.z=0;const d=to.clone().sub(from),len=d.length();d.normalize();const r=new THREE.Vector3().crossVectors(d,new THREE.Vector3(0,0,1)).normalize(),u=new THREE.Vector3().crossVectors(r,d);n.position.copy(from);n.quaternion.setFromRotationMatrix(new THREE.Matrix4().makeBasis(r,d,u));n.scale.setScalar(len/100);paint(n,[.7,.72,1],alpha);}
 flow.top.updateMatrixWorld(true);for(const n of flow.facing){const q=new THREE.Quaternion();n.parent.getWorldQuaternion(q);n.quaternion.copy(q.invert().multiply(camera.quaternion));}
 $('timeline').value=String(Math.min(1,age/run.seconds)*1000);$('status').textContent=`${run.dps} damage / second · ${run.duration}s channel · ${Math.round(run.reach/70*10)/10}m reach`;
 $('phase').textContent=age<.4?'Manifest':age<look.release?'Release':age<look.release+run.duration?'Channel':age<run.seconds?'Fade':'';renderer.render(scene,camera);
}draw();addEventListener('resize',()=>{camera.aspect=innerWidth/innerHeight;camera.updateProjectionMatrix();renderer.setSize(innerWidth,innerHeight);});

