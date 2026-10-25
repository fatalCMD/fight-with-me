import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {SoundPlayer} from './sound-player.js';
const $=s=>document.querySelector(s),scene=new THREE.Scene();scene.background=new THREE.Color('#14171c');
const stage=$('#stage'),renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));stage.append(renderer.domElement);
const camera=new THREE.PerspectiveCamera(42,1,.1,2200);camera.up.set(0,0,1);
const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,0,52);controls.enableDamping=true;
const views={front:[250,-430,300],side:[480,0,270],rear:[-250,430,300],high:[180,-240,520]};
function angle(){camera.position.fromArray(views[$('#angle').value]);controls.update();}angle();$('#angle').onchange=angle;
function resize(){renderer.setSize(stage.clientWidth,stage.clientHeight);camera.aspect=stage.clientWidth/stage.clientHeight;camera.updateProjectionMatrix();}resize();addEventListener('resize',resize);
const ambient=new THREE.HemisphereLight(0xc9d3e3,0x242029,1.8),sun=new THREE.DirectionalLight(0xffe7cb,2);sun.position.set(-180,-240,400);scene.add(ambient,sun);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(2000,2000),new THREE.MeshStandardMaterial({color:0x29292c,roughness:1}));ground.position.z=-1;scene.add(ground);
const grid=new THREE.GridHelper(1400,28,0x474245,0x333237);grid.rotation.x=Math.PI/2;grid.material.transparent=true;grid.material.opacity=.25;scene.add(grid);
// A neutral armored scale figure, deliberately separate from the exported effect mesh.
const figure=new THREE.Group();scene.add(figure);
const armor=new THREE.MeshStandardMaterial({color:0x4c5059,roughness:.75,metalness:.25}),cloth=new THREE.MeshStandardMaterial({color:0x282c35,roughness:1});
function limb(radius,length,at,material=armor,tilt=0){const mesh=new THREE.Mesh(new THREE.CapsuleGeometry(radius,length,6,18),material);mesh.rotation.set(Math.PI/2,tilt,0);mesh.position.fromArray(at);figure.add(mesh);return mesh;}
limb(17,30,[0,0,91]);limb(11,7,[0,-1,140]);limb(8,45,[-12,0,37],cloth);limb(8,45,[12,0,37],cloth);
limb(7,31,[-27,-3,89],armor,-.16);limb(7,31,[27,-3,89],armor,.16);
for(const x of [-25,25]){const p=new THREE.Mesh(new THREE.SphereGeometry(13,16,8),armor);p.scale.set(1,.85,.65);p.position.set(x,0,112);figure.add(p);}
let elapsed=0,paused=false,last=performance.now(),rank=Number(new URL(location.href).searchParams.get('rank'))||1;
const glow=new THREE.PointLight(0xa17aff,0,330,2);glow.position.set(0,0,18);scene.add(glow);
const loader=new THREE.TextureLoader(),textures=new Map(),nodes={},billboards=[],materials=[];
const [mesh,look]=await Promise.all(['surge.json','surge-look.json'].map(async file=>{const response=await fetch('out/'+file);if(!response.ok)throw Error(file+' unavailable');return response.json();}));
const audioData=await fetch('out/audio-plans.json').then(r=>r.json()),sound=new SoundPlayer(audioData,$('#audio'),$('#sound'),$('#soundStatus'));
const root=new THREE.Group();scene.add(root);
async function addMesh(mesh,prefix=''){
const local={};
for(const n of mesh.nodes){const group=new THREE.Group();group.position.fromArray(n.translate);nodes[prefix+n.name]=local[n.name]=group;if(n.billboard)billboards.push(group);}
for(const n of mesh.nodes)(local[n.parent]||root).add(local[n.name]);
for(const s of mesh.shapes){
 if(!textures.has(s.diffuse)){const t=await loader.loadAsync('out/'+s.diffuse);t.wrapS=t.wrapT=THREE.RepeatWrapping;textures.set(s.diffuse,t);}
 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(s.positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(s.uvs,2));g.setAttribute('shade',new THREE.Float32BufferAttribute(s.colors,4));g.setIndex(s.indices);
 const material=new THREE.ShaderMaterial({uniforms:{map:{value:textures.get(s.diffuse)},tint:{value:new THREE.Vector3(1,.3,.05)},power:{value:0},drift:{value:0}},vertexShader:'attribute vec4 shade;varying vec2 vUv;varying vec4 vShade;void main(){vUv=uv;vShade=shade;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform vec3 tint;uniform float power;uniform float drift;varying vec2 vUv;varying vec4 vShade;void main(){vec4 t=texture2D(map,vUv+vec2(0.,drift));gl_FragColor=vec4(t.rgb*tint*vShade.rgb*power,t.a*vShade.a);}',transparent:true,depthWrite:false,side:THREE.DoubleSide,blending:THREE.AdditiveBlending});
 material.userData.scale=s.effect.scale;material.userData.drifting=/HealingPulse|cloudtilefrostsmall|darkswirlsstrip/i.test(s.source);materials.push(material);
 const object=new THREE.Mesh(g,material);object.position.fromArray(s.transform.translate);local[s.node].add(object);
}
}
await addMesh(mesh);
const [support,supportLook]=await Promise.all(['support-Surge.json','support-look.json'].map(f=>fetch('out/'+f).then(r=>r.json())));
await addMesh(support,'Aura_');
const auraParts=['FWM_Core','FWM_Wisps',...Array.from({length:12},(_,i)=>'MoteAt'+i)].map(name=>{const node=nodes['Aura_'+name],m=[];node.traverse(o=>{if(o.material)m.push(o.material)});return {node,materials:m,origin:node.position.clone()};});
const parts=look.names.map(name=>({node:nodes[name],materials:[]}));for(const part of parts)part.node.traverse(o=>{if(o.material)part.materials.push(o.material);});
function setRank(value){rank=Math.max(1,Math.min(3,value));document.querySelectorAll('[data-rank]').forEach(b=>b.setAttribute('aria-pressed',Number(b.dataset.rank)===rank));$('#subtitle').textContent=rank===3?'A greater seal. A deeper surge.':'An arcane seal follows the caster.';elapsed=0;paused=false;sound.load(audioData.plans.surge[rank-1],12);$('#pause').textContent='Pause';history.replaceState(null,'','?rank='+rank);}
document.querySelectorAll('[data-rank]').forEach(b=>b.onclick=()=>setRank(Number(b.dataset.rank)));setRank(rank);
$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Play':'Pause';};$('#replay').onclick=()=>{elapsed=0;paused=false;sound.reset();sound.unlock();$('#pause').textContent='Pause';};
$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;$('#pause').textContent='Play';};
$('#day').onchange=()=>{const day=$('#day').checked;scene.background.set(day?'#8993a2':'#14171c');ambient.intensity=day?3:1.8;sun.intensity=day?3.5:2;};
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;if(!paused){const before=elapsed;elapsed=(elapsed+dt)%(look.seconds+1);if(elapsed<before)sound.reset();}sound.update(elapsed,!paused,1);
 const t=Math.min(elapsed,look.seconds),frames=look.ranks[rank-1],at=Math.min(t*look.fps,frames.length-1),lo=Math.floor(at),f=at-lo,a=frames[lo],b=frames[Math.min(lo+1,frames.length-1)];
 const moving=$('#move').checked,dx=moving?Math.sin(t*.45)*75:0,dy=moving?Math.sin(t*.7)*20:0;figure.position.set(dx,dy,0);
 parts.forEach((part,i)=>{const p=a.parts[i].map((v,j)=>v+(b.parts[i][j]-v)*f);part.node.position.set(p[0]+dx,p[1]+dy,p[2]);part.node.scale.setScalar(p[3]);part.node.rotation.set(p[5],0,p[4],'ZYX');part.node.visible=p[6]>.0001;for(const mat of part.materials){mat.uniforms.tint.value.set(p[7],p[8],p[9]);mat.uniforms.power.value=p[6]*mat.userData.scale;mat.uniforms.drift.value=mat.userData.drifting?p[10]:0;}});
 const af=supportLook.effects.Surge.frames[Math.min(Math.floor(t*supportLook.fps),supportLook.effects.Surge.frames.length-1)];
 auraParts.forEach((part,i)=>{const node=part.node;node.visible=t<12;if(i<2){node.position.set(dx,dy,0);node.rotation.z=i===0?af.coreTurn:af.turn;node.scale.setScalar(i===0?af.coreScale:af.scale);for(const mat of part.materials){mat.uniforms.tint.value.set(.73,.47,1);mat.uniforms.power.value=af.alpha*mat.userData.scale;mat.uniforms.drift.value=mat.userData.drifting?(i===0?af.coreDrift:af.drift):0;}}else{const [angle,rise,alpha,scale]=af.motes[i-2],o=part.origin;node.position.set(dx+Math.cos(angle)*o.x-Math.sin(angle)*o.y,dy+Math.sin(angle)*o.x+Math.cos(angle)*o.y,o.z+rise);node.scale.setScalar(scale);for(const mat of part.materials){mat.uniforms.tint.value.set(.73,.47,1);mat.uniforms.power.value=alpha*mat.userData.scale;}}});
 root.updateMatrixWorld(true);for(const group of billboards){const q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}
 glow.position.x=dx;glow.position.y=dy;glow.color.setRGB(.56,.4,1);glow.intensity=(a.light+(b.light-a.light)*f)*1100;
 $('#timeline').value=t;$('#phase').textContent=(t<.6?'Gathering':t<12?'Surge active':t<look.seconds?'Fading':'Ended')+' · '+t.toFixed(2)+' s';controls.update();renderer.render(scene,camera);
}draw();
