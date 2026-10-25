import * as THREE from 'three';

import {OrbitControls} from 'three/addons/controls/OrbitControls.js';

const $=s=>document.querySelector(s),scene=new THREE.Scene();scene.background=new THREE.Color('#14171c');

const stage=$('#stage'),renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));stage.append(renderer.domElement);

const camera=new THREE.PerspectiveCamera(42,1,.1,2200);camera.up.set(0,0,1);

const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,0,83);controls.enableDamping=true;

const views={front:[190,-330,220],side:[370,0,210],rear:[-180,350,220],high:[180,-200,420]};

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

const blade=new THREE.Mesh(new THREE.BoxGeometry(6,2,92),new THREE.MeshStandardMaterial({color:0x88909a,metalness:.75,roughness:.3}));blade.position.set(31,-7,47);blade.rotation.y=.18;figure.add(blade);

const hilt=new THREE.Mesh(new THREE.BoxGeometry(22,5,4),armor);hilt.position.set(23,-7,86);figure.add(hilt);

let elapsed=0,paused=false,last=performance.now(),rank=Number(new URL(location.href).searchParams.get('rank'))||1;

const glow=new THREE.PointLight(0xff5421,0,330,2);glow.position.set(0,0,70);scene.add(glow);

const loader=new THREE.TextureLoader(),textures=new Map(),nodes={},billboards=[],materials=[];

const [mesh,look]=await Promise.all(['disarm.json','disarm-look.json'].map(async file=>{const response=await fetch('out/'+file);if(!response.ok)throw Error(file+' unavailable');return response.json();}));

const root=new THREE.Group();scene.add(root);

for(const n of mesh.nodes){const group=new THREE.Group();group.position.fromArray(n.translate);group.name=n.name;nodes[n.name]=group;if(n.billboard)billboards.push(group);}

for(const n of mesh.nodes)(nodes[n.parent]||root).add(nodes[n.name]);

for(const s of mesh.shapes){

 if(!textures.has(s.diffuse)){const t=await loader.loadAsync('out/'+s.diffuse);t.wrapS=t.wrapT=THREE.RepeatWrapping;textures.set(s.diffuse,t);}

 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(s.positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(s.uvs,2));g.setAttribute('shade',new THREE.Float32BufferAttribute(s.colors,4));g.setIndex(s.indices);

 const material=new THREE.ShaderMaterial({uniforms:{map:{value:textures.get(s.diffuse)},tint:{value:new THREE.Vector3(1,.3,.05)},power:{value:0},drift:{value:0}},vertexShader:'attribute vec4 shade;varying vec2 vUv;varying vec4 vShade;void main(){vUv=uv;vShade=shade;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform vec3 tint;uniform float power;uniform float drift;varying vec2 vUv;varying vec4 vShade;void main(){vec4 t=texture2D(map,vUv+vec2(0.,drift));gl_FragColor=vec4(t.rgb*tint*vShade.rgb*power,t.a*vShade.a);}',transparent:true,depthWrite:false,side:THREE.DoubleSide,blending:THREE.AdditiveBlending});

 material.userData.scale=s.effect.scale;material.userData.drifting=/FXFireScrollTileGrey/i.test(s.source);materials.push(material);

 const object=new THREE.Mesh(g,material);object.position.fromArray(s.transform.translate);nodes[s.node].add(object);

}

const parts=look.names.map(name=>({node:nodes[name],materials:[]}));for(const part of parts)part.node.traverse(o=>{if(o.material)part.materials.push(o.material);});

const extraEffects=[-140,140].map(offset=>{const copy=root.clone(true);copy.userData.offset=offset;scene.add(copy);return copy;});

const leftFigure=figure.clone(true),rightFigure=figure.clone(true);leftFigure.position.x=-140;rightFigure.position.x=140;scene.add(leftFigure,rightFigure);

function setRank(value){rank=Math.max(1,Math.min(3,value));document.querySelectorAll('[data-rank]').forEach(b=>b.setAttribute('aria-pressed',Number(b.dataset.rank)===rank));$('#subtitle').textContent=rank===3?'Crimson crosscut · white contact flash':'Blue cut · flying weapon sparks';elapsed=0;paused=false;$('#pause').textContent='Pause';history.replaceState(null,'','?rank='+rank);}

document.querySelectorAll('[data-rank]').forEach(b=>b.onclick=()=>setRank(Number(b.dataset.rank)));setRank(rank);

$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Play':'Pause';};$('#replay').onclick=()=>{elapsed=0;paused=false;$('#pause').textContent='Pause';};

$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;$('#pause').textContent='Play';};

$('#day').onchange=()=>{const day=$('#day').checked;scene.background.set(day?'#8993a2':'#14171c');ambient.intensity=day?3:1.8;sun.intensity=day?3.5:2;};

function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;if(!paused){const before=elapsed;elapsed=(elapsed+dt)%(look.seconds+1.1);}

 const t=Math.min(elapsed,look.seconds),frames=look.ranks[rank-1],at=Math.min(t*look.fps,frames.length-1),lo=Math.floor(at),f=at-lo,a=frames[lo],b=frames[Math.min(lo+1,frames.length-1)];

 root.position.set(31,-7,57);

 parts.forEach((part,i)=>{const p=a.parts[i].map((v,j)=>v+(b.parts[i][j]-v)*f);part.node.position.set(p[0],p[1],p[2]);part.node.scale.setScalar(Math.max(.001,p[3]));part.node.visible=p[4]>.0001;for(const mat of part.materials){mat.uniforms.tint.value.set(p[5],p[6],p[7]);mat.uniforms.power.value=p[4]*mat.userData.scale;}});

 const multiple=$('#move').checked;leftFigure.visible=rightFigure.visible=multiple;

 root.updateMatrixWorld(true);for(const group of billboards){const q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}

 glow.position.copy(root.position);glow.color.setRGB(rank===3?1:.045,rank===3?1:.35,1);glow.intensity=(a.light+(b.light-a.light)*f)*1100;

 $('#timeline').value=t;$('#phase').textContent=(t<.06?'Contact':t<.20?(rank===3?'Crosscut':'Slash'):t<.36?'Slashes fading':t<look.seconds?'Sparks fading':'Ended')+' · '+t.toFixed(2)+' s';for(const other of extraEffects){other.visible=multiple;other.position.set(root.position.x+other.userData.offset,root.position.y,root.position.z);for(const name of look.names){const from=nodes[name],to=other.getObjectByName(name);to.position.copy(from.position);to.scale.copy(from.scale);to.visible=from.visible;}}

 for(const other of extraEffects){other.updateMatrixWorld(true);for(const source of billboards){const group=other.getObjectByName(source.name),q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}}controls.update();renderer.render(scene,camera);

}draw();

