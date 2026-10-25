import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {SoundPlayer} from './sound-player.js';
const $=s=>document.querySelector(s),scene=new THREE.Scene();scene.background=new THREE.Color('#14171c');
const stage=$('#stage'),renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));stage.append(renderer.domElement);
const camera=new THREE.PerspectiveCamera(42,1,.1,2500);camera.up.set(0,0,1);
const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,20,80);controls.enableDamping=true;
const views={front:[390,-490,250],side:[580,30,240],rear:[-330,520,280],high:[250,-220,630]};
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
 shield.visible=false;weapon.visible=false;return {group,body,arms,weapon};
}
const fighter=figure(0x526679),enemy=figure(0x775448);fighter.group.position.y=-150;fighter.group.rotation.z=Math.PI;enemy.group.position.y=280;
const bow=new THREE.Group();bow.position.set(0,-115,100);scene.add(bow);
const curve=new THREE.CatmullRomCurve3([new THREE.Vector3(0,-8,-48),new THREE.Vector3(0,4,-25),new THREE.Vector3(0,0,0),new THREE.Vector3(0,4,25),new THREE.Vector3(0,-8,48)]);
bow.add(new THREE.Mesh(new THREE.TubeGeometry(curve,48,1.9,8,false),new THREE.MeshStandardMaterial({color:0xbba788,roughness:.7})));
const string=new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(0,-8,-48),new THREE.Vector3(0,-25,0),new THREE.Vector3(0,-8,48)]),new THREE.LineBasicMaterial({color:0xc4c0ae}));bow.add(string);
const arrows=[];for(let i=0;i<5;i++){const a=new THREE.Mesh(new THREE.CylinderGeometry(.7,.7,64,6),new THREE.MeshStandardMaterial({color:0xb4afa2,roughness:.6}));scene.add(a);arrows.push(a);}
const ease=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
let elapsed=0,paused=false,last=performance.now(),rank=Number(new URL(location.href).searchParams.get('rank'))||1;
const [mesh,look,audio]=await Promise.all(['arrowarts.json','arrowarts-look.json','audio-plans.json'].map(async file=>{const r=await fetch('out/'+file);if(!r.ok)throw Error(file+' unavailable');return r.json();}));
const sounds=new SoundPlayer(audio,$('#audio'),$('#sound'),$('#soundStatus')),loader=new THREE.TextureLoader(),textures=new Map();
for(const s of mesh.shapes)if(!textures.has(s.diffuse)){const t=await loader.loadAsync('out/'+s.diffuse);t.wrapS=t.wrapT=THREE.RepeatWrapping;textures.set(s.diffuse,t);}
function makeEffect(kind){
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
 return {root,parts,billboards,light,kind};
}
const gusts=Array.from({length:5},()=>makeEffect(0)),rune=makeEffect(1);
// Native Skyrim runs FireBallExp01.nif's particle controllers. This browser approximation
// uses that explosion's actual animated flame atlas; the rune and gust are the shipped meshes.
const expAssets=await (await fetch('out/arrow-explosion.json')).json();
const fireAtlas=await loader.loadAsync('out/'+expAssets.fire);
fireAtlas.wrapS=fireAtlas.wrapT=THREE.ClampToEdgeWrapping;
const explosion=new THREE.Group();explosion.position.set(0,280,95);scene.add(explosion);
const puffs=[];
for(let i=0;i<22;i++){
 const material=new THREE.ShaderMaterial({uniforms:{map:{value:fireAtlas},frame:{value:0},fade:{value:0},heat:{value:0}},
 vertexShader:'varying vec2 vUv;varying float height;void main(){vUv=uv;height=(modelMatrix*vec4(position,1.)).z;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',
 fragmentShader:`uniform sampler2D map;uniform float frame,fade,heat;varying vec2 vUv;varying float height;
 vec4 flame(float f){return texture2D(map,(vec2(mod(f,8.),3.-floor(f/8.))+clamp(vUv,.004,.996))/vec2(8.,4.));}
 void main(){float f=mod(frame,32.);vec4 t=mix(flame(floor(f)),flame(mod(floor(f)+1.,32.)),fract(f));
 float v=t.r;float edge=1.-smoothstep(.37,.5,length(vUv-.5));
 vec3 color=mix(vec3(.55,.035,.003),vec3(1.,.28,.015),smoothstep(.06,.35,v));
 color=mix(color,vec3(1.,.85,.38),smoothstep(.33,.9,v)*heat);
 gl_FragColor=vec4(color*(.8+heat*.5),v*fade*edge*smoothstep(0.,18.,height));}`,transparent:true,depthWrite:false,blending:THREE.AdditiveBlending,side:THREE.DoubleSide});
 const puff=new THREE.Mesh(new THREE.PlaneGeometry(1,1),material);explosion.add(puff);puffs.push(puff);
}
const flashTex=await loader.loadAsync('out/'+expAssets.glow);
const flash=new THREE.Mesh(new THREE.PlaneGeometry(1,1),new THREE.ShaderMaterial({uniforms:{map:{value:flashTex},fade:{value:0}},vertexShader:'varying vec2 vUv;void main(){vUv=uv;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform float fade;varying vec2 vUv;void main(){float r=length(vUv-.5)*2.;float a=texture2D(map,vUv).r*pow(max(0.,1.-r*r),3.)*fade;gl_FragColor=vec4(1.,.65,.23,a);}',transparent:true,depthWrite:false,blending:THREE.AdditiveBlending}));explosion.add(flash);
const fireLight=new THREE.PointLight(0xff8a30,0,650,2);explosion.add(fireLight);
function poseExplosion(age){
 explosion.visible=age>=0&&age<1.9;if(!explosion.visible)return;
 const size=.85+.25*(rank-1),growth=1-Math.exp(-age*7);
 puffs.forEach((p,i)=>{const delay=i*.006,t=age-delay,angle=i*2.399963,y=1-2*(i+.5)/puffs.length,r=Math.sqrt(1-y*y);
  p.visible=t>=0;if(!p.visible)return;
  const travel=growth*(i<4?25:100)*size;
  p.position.set(Math.cos(angle)*r*travel,Math.sin(angle)*r*travel,y*travel+age*35);
  p.quaternion.copy(camera.quaternion);p.rotateZ(angle+age*(i%2?-.22:.28));
  p.scale.setScalar(size*(35+growth*(i<4?170:115)+age*30));
  p.material.uniforms.frame.value=t*22+i*1.7;
  p.material.uniforms.fade.value=Math.min(1,t*22)*Math.pow(Math.max(0,1-t/1.65),1.65)*(i<4?.9:.62);
  p.material.uniforms.heat.value=Math.max(0,1-age*.75);
 });
 flash.scale.setScalar(size*(100+growth*370));flash.quaternion.copy(camera.quaternion);flash.material.uniforms.fade.value=Math.max(0,1-age/.2)*.65;
 fireLight.intensity=4500*Math.max(0,1-age/1.1);
}
let ability=new URL(location.href).searchParams.get("ability")||"explosive";$("#ability").value=ability;
function pose(effect,age,at){
 effect.root.position.fromArray(at);effect.root.visible=age>=0&&age<=3;effect.light.intensity=0;if(!effect.root.visible)return;
 const frames=look.kinds[effect.kind][rank-1],sample=Math.min(age*look.fps,frames.length-1),lo=Math.floor(sample),f=sample-lo,a=frames[lo],b=frames[Math.min(lo+1,frames.length-1)];
 effect.parts.forEach((part,i)=>{const p=a.parts[i].map((v,j)=>v+(b.parts[i][j]-v)*f);part.node.position.set(p[0],p[1],p[2]);part.node.rotation.set(effect.kind===2&&i>=4&&i<7?-(i-4)*Math.PI/3:0,p[5],0);part.node.scale.setScalar(Math.max(.001,p[3]));part.node.visible=p[4]>.0001;for(const mat of part.materials){mat.uniforms.tint.value.set(p[6],p[7],p[8]);mat.uniforms.power.value=p[4]*mat.userData.scale;}});
 effect.root.updateMatrixWorld(true);for(const group of effect.billboards){const q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}
 effect.light.position.copy(effect.root.position);effect.light.intensity=(a.light+(b.light-a.light)*f)*1100;
}
function setRank(value){rank=Math.max(1,Math.min(3,value));document.querySelectorAll('[data-rank]').forEach(b=>b.setAttribute('aria-pressed',Number(b.dataset.rank)===rank));
 $('h1').textContent=ability==='volley'?'Volley':'Explosive Arrow';$('#subtitle').textContent=ability==='volley'?'A brief gust at the arrow rest, on every shot':rank===3?'Emerald centre / crimson outer rune':'A turning emerald spell circle';
 const cues=ability==='volley'?Array.from({length:rank+2},(_,i)=>audio.plans.volley[rank-1].map(c=>({...c,at:c.at+.65+i*.28}))).flat():audio.plans['explosive-impact'][rank-1].map(c=>({...c,at:c.at+1.65}));
 sounds.load(cues,3.4);elapsed=0;paused=false;$('#pause').textContent='Pause';history.replaceState(null,'','?ability='+ability+'&rank='+rank);
}
$('#ability').onchange=()=>{ability=$('#ability').value;setRank(rank);};

document.querySelectorAll('[data-rank]').forEach(b=>b.onclick=()=>setRank(Number(b.dataset.rank)));setRank(rank);
$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Play':'Pause';sounds.unlock();};$('#replay').onclick=()=>{elapsed=0;paused=false;sounds.reset();sounds.unlock();$('#pause').textContent='Pause';};
$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;sounds.reset();$('#pause').textContent='Play';};
$('#day').onchange=()=>{const day=$('#day').checked;scene.background.set(day?'#8993a2':'#14171c');ambient.intensity=day?3:1.8;sun.intensity=day?3.5:2;};
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;if(!paused){elapsed+=dt;if(elapsed>3.4){elapsed=0;sounds.reset();}}
 const t=elapsed;fighter.arms[0].rotation.x=-1.1;fighter.arms[1].rotation.x=-1;fighter.arms[1].rotation.z=-.6;
 const count=ability==='volley'?rank+2:1;
 for(let i=0;i<5;i++){
  const age=t-.65-i*.28;arrows[i].visible=i<count&&age>=0&&age<1;arrows[i].position.set(0,-115+395*age,100-5*age);
  pose(gusts[i],ability==='volley'&&i<count?age:-1,[0,-115,100]);
 }
 pose(rune,ability==='explosive'?t:-1,[0,-198,100]);poseExplosion(ability==='explosive'?t-1.65:-1);
 enemy.body.rotation.x=ability==='explosive'?-.18*Math.sin(Math.PI*ease((t-1.65)/.7)):0;
 sounds.update(t,!paused,1);$('#timeline').value=t;
 $('#phase').textContent=(ability==='volley'?(t<.65?'Ready':t<.65+(rank+2)*.28?'Volley firing':'Gusts fading'):(t<.65?'Rune charging':t<1.65?'Arrow in flight':t<2.3?'Detonation / rune flash':'Fading'))+' / '+t.toFixed(2)+' s';
 controls.update();renderer.render(scene,camera);
}draw();
