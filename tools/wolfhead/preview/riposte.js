import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {SoundPlayer} from './sound-player.js';
const $=s=>document.querySelector(s),scene=new THREE.Scene();scene.background=new THREE.Color('#14171c');
const stage=$('#stage'),renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));stage.append(renderer.domElement);
const camera=new THREE.PerspectiveCamera(42,1,.1,2500);camera.up.set(0,0,1);
const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,50,65);controls.enableDamping=true;
const views={front:[360,-420,270],side:[540,60,210],rear:[-330,460,260],high:[250,-170,570]};
function angle(){camera.position.fromArray(views[$('#angle').value]);controls.update();}angle();$('#angle').onchange=angle;
function resize(){renderer.setSize(stage.clientWidth,stage.clientHeight);camera.aspect=stage.clientWidth/stage.clientHeight;camera.updateProjectionMatrix();}resize();addEventListener('resize',resize);
const ambient=new THREE.HemisphereLight(0xc9d3e3,0x242029,1.8),sun=new THREE.DirectionalLight(0xffe7cb,2);sun.position.set(-180,-240,400);scene.add(ambient,sun);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(2000,2000),new THREE.MeshStandardMaterial({color:0x29292c,roughness:1}));ground.position.z=-1;scene.add(ground);
const grid=new THREE.GridHelper(1400,28,0x474245,0x333237);grid.rotation.x=Math.PI/2;grid.material.transparent=true;grid.material.opacity=.2;scene.add(grid);

// Scale figures demonstrate the sequence. Skyrim supplies its own combat animations and ragdoll.
function figure(color){
 const group=new THREE.Group(),body=new THREE.Group();group.add(body);scene.add(group);
 const armor=new THREE.MeshStandardMaterial({color,roughness:.65,metalness:.35}),cloth=new THREE.MeshStandardMaterial({color:0x282c35,roughness:1});
 function limb(radius,length,at,material=armor){const mesh=new THREE.Mesh(new THREE.CapsuleGeometry(radius,length,6,18),material);mesh.rotation.x=Math.PI/2;mesh.position.fromArray(at);body.add(mesh);return mesh;}
 limb(17,30,[0,0,91]);limb(11,7,[0,0,140]);limb(8,45,[-12,0,37],cloth);limb(8,45,[12,0,37],cloth);
 const arms=[];for(const x of [-25,25]){const joint=new THREE.Group();joint.position.set(x,0,111);body.add(joint);const arm=new THREE.Mesh(new THREE.CapsuleGeometry(7,35,6,14),armor);arm.rotation.x=Math.PI/2;arm.position.z=-21;joint.add(arm);arms.push(joint);}
 const weapon=new THREE.Group();weapon.position.set(0,0,-43);arms[1].add(weapon);
 const blade=new THREE.Mesh(new THREE.BoxGeometry(5,3,70),new THREE.MeshStandardMaterial({color:0xa5aab4,metalness:.8,roughness:.25}));blade.position.z=32;weapon.add(blade);
 const hilt=new THREE.Mesh(new THREE.BoxGeometry(22,5,4),armor);weapon.add(hilt);
 const shield=new THREE.Mesh(new THREE.CylinderGeometry(21,21,5,48),armor);shield.position.set(0,-4,-32);arms[0].add(shield);
 return {group,body,arms,weapon};
}
const fighter=figure(0x526679),enemy=figure(0x775448);fighter.group.position.y=-95;fighter.group.rotation.z=Math.PI;enemy.group.position.y=55;
const ease=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
let elapsed=0,paused=false,last=performance.now(),rank=Number(new URL(location.href).searchParams.get('rank'))||1;
const [mesh,look,audio]=await Promise.all(['riposte.json','riposte-look.json','audio-plans.json'].map(async file=>{const r=await fetch('out/'+file);if(!r.ok)throw Error(file+' unavailable');return r.json();}));
const sounds=new SoundPlayer(audio,$('#audio'),$('#sound'),$('#soundStatus')),loader=new THREE.TextureLoader(),textures=new Map();
for(const s of mesh.shapes)if(!textures.has(s.diffuse)){const t=await loader.loadAsync('out/'+s.diffuse);t.wrapS=t.wrapT=THREE.RepeatWrapping;textures.set(s.diffuse,t);}
function makeEffect(isGround){
 const root=new THREE.Group(),nodes={},billboards=[];scene.add(root);
 for(const n of mesh.nodes){const group=new THREE.Group();group.position.fromArray(n.translate);group.name=n.name;nodes[n.name]=group;if(n.billboard)billboards.push(group);}
 for(const n of mesh.nodes)(nodes[n.parent]||root).add(nodes[n.name]);
 for(const s of mesh.shapes){
  const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(s.positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(s.uvs,2));g.setAttribute('shade',new THREE.Float32BufferAttribute(s.colors,4));g.setIndex(s.indices);
  const material=new THREE.ShaderMaterial({uniforms:{map:{value:textures.get(s.diffuse)},tint:{value:new THREE.Vector3()},power:{value:0}},vertexShader:'attribute vec4 shade;varying vec2 vUv;varying vec4 vShade;void main(){vUv=uv;vShade=shade;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform vec3 tint;uniform float power;varying vec2 vUv;varying vec4 vShade;void main(){vec4 t=texture2D(map,vUv);gl_FragColor=vec4(t.rgb*tint*vShade.rgb*power,t.a*vShade.a);}',transparent:true,depthWrite:false,side:THREE.DoubleSide,blending:THREE.AdditiveBlending});
  material.userData.scale=s.effect.scale;const object=new THREE.Mesh(g,material);object.position.fromArray(s.transform.translate);nodes[s.node].add(object);
 }
 const parts=look.names.map(name=>{const node=nodes[name],materials=[];node.traverse(o=>{if(o.material)materials.push(o.material);});return{node,materials};});
 const light=new THREE.PointLight(0xffffff,0,300,2);scene.add(light);
 return {root,parts,billboards,light,isGround};
}
const contact=makeEffect(false),landing=makeEffect(true);
function pose(effect,age,at){
 effect.root.position.fromArray(at);effect.root.visible=age>=0&&age<=look.seconds;effect.light.intensity=0;if(!effect.root.visible)return;
 const frames=look.variants[effect.isGround?1:0][rank-1],sample=Math.min(age*look.fps,frames.length-1),lo=Math.floor(sample),f=sample-lo,a=frames[lo],b=frames[Math.min(lo+1,frames.length-1)];
 effect.parts.forEach((part,i)=>{const p=a.parts[i].map((v,j)=>v+(b.parts[i][j]-v)*f);part.node.position.set(p[0],p[1],p[2]);part.node.rotation.x=effect.isGround?0:-Math.PI/2;part.node.scale.setScalar(Math.max(.001,p[3]));part.node.visible=p[4]>.0001;for(const mat of part.materials){mat.uniforms.tint.value.set(p[5],p[6],p[7]);mat.uniforms.power.value=p[4]*mat.userData.scale;}});
 effect.root.updateMatrixWorld(true);for(const group of effect.billboards){const q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}
 effect.light.position.copy(effect.root.position);effect.light.intensity=(a.light+(b.light-a.light)*f)*1100;
}
function setRank(value){rank=Math.max(1,Math.min(3,value));document.querySelectorAll('[data-rank]').forEach(b=>b.setAttribute('aria-pressed',Number(b.dataset.rank)===rank));$('#subtitle').textContent=(rank===3?'Crimson double impact':'Steel-blue parry')+' · '+(rank+.5)+' s knockdown';
 const cues=[...audio.plans.riposte[rank-1].map(c=>({...c,at:c.at+1})),...audio.plans['riposte-impact'][rank-1].map(c=>({...c,at:c.at+1.9}))];sounds.load(cues,7);elapsed=0;paused=false;$('#pause').textContent='Pause';history.replaceState(null,'','?rank='+rank);
}
document.querySelectorAll('[data-rank]').forEach(b=>b.onclick=()=>setRank(Number(b.dataset.rank)));setRank(rank);
$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Play':'Pause';sounds.unlock();};$('#replay').onclick=()=>{elapsed=0;paused=false;sounds.reset();sounds.unlock();$('#pause').textContent='Pause';};
$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;sounds.reset();$('#pause').textContent='Play';};
$('#day').onchange=()=>{const day=$('#day').checked;scene.background.set(day?'#8993a2':'#14171c');ambient.intensity=day?3:1.8;sun.intensity=day?3.5:2;};
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;if(!paused){elapsed+=dt;if(elapsed>7){elapsed=0;sounds.reset();}}
 const t=elapsed,guard=ease(t/.5)*(1-ease((t-1.15)/.35));fighter.arms[0].rotation.x=-1.2*guard;fighter.arms[1].rotation.x=-.85*guard-.65*Math.sin(Math.PI*ease((t-1)/.5));
 const strike=ease((t-.5)/.5)*(1-ease((t-1)/.3));enemy.arms[1].rotation.x=2.4*strike;
 const flight=Math.max(0,Math.min(1,(t-1.22)/.68)),getUp=ease((t-(1.9+rank+.5))/.8),fall=ease(flight)*(1-getUp);
 enemy.group.position.y=55+105*ease(flight);enemy.body.rotation.x=-Math.PI*.5*fall;
 enemy.group.position.z=18*fall+Math.sin(Math.PI*flight)*35*(1-getUp);
 enemy.arms[0].rotation.x=.6*fall;enemy.arms[1].rotation.z=.4*fall;
 pose(contact,t-1,[0,-20,100]);pose(landing,t-1.9,[0,245,2]);sounds.update(t,!paused,1);
 $('#timeline').value=t;$('#phase').textContent=(t<1?'Guard':t<1.22?'Parry & counter':t<1.9?'Knockback':t<1.9+rank+.5?'Downed':t<2.7+rank+.5?'Recovering':'Ready')+' · '+t.toFixed(2)+' s';
 controls.update();renderer.render(scene,camera);
}draw();
