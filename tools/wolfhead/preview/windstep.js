import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {SoundPlayer} from './sound-player.js';
const $=s=>document.querySelector(s),scene=new THREE.Scene();scene.background=new THREE.Color('#14171c');
const stage=$('#stage'),renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));stage.append(renderer.domElement);
const camera=new THREE.PerspectiveCamera(42,1,.1,2200);camera.up.set(0,0,1);
const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,-100,75);controls.enableDamping=true;
const views={front:[300,380,250],side:[470,-110,220],rear:[-280,-570,250],high:[240,-170,570],detail:[210,150,150]};
function angle(){controls.target.set(0,$('#angle').value==='detail'?15:-100,$('#angle').value==='detail'?85:75);camera.position.fromArray(views[$('#angle').value]);controls.update();}angle();$('#angle').onchange=angle;
function resize(){renderer.setSize(stage.clientWidth,stage.clientHeight);camera.aspect=stage.clientWidth/stage.clientHeight;camera.updateProjectionMatrix();}resize();addEventListener('resize',resize);
const ambient=new THREE.HemisphereLight(0xc9d3e3,0x242029,1.8),sun=new THREE.DirectionalLight(0xffe7cb,2);sun.position.set(-180,-240,400);scene.add(ambient,sun);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(2000,2000),new THREE.MeshStandardMaterial({color:0x29292c,roughness:1}));ground.position.z=-1;scene.add(ground);
const grid=new THREE.GridHelper(1400,28,0x474245,0x333237);grid.rotation.x=Math.PI/2;grid.material.transparent=true;grid.material.opacity=.25;scene.add(grid);
// Opaque stock archer represents the moving follower; the exported ghost stays behind.
const figure=new THREE.Group();scene.add(figure);
let elapsed=0,paused=false,last=performance.now(),rank=Number(new URL(location.href).searchParams.get('rank'))||1;
const glow=new THREE.PointLight(0xff5421,0,330,2);glow.position.set(0,0,70);scene.add(glow);
const loader=new THREE.TextureLoader(),textures=new Map(),nodes={},billboards=[],materials=[];
const female=new URL(location.href).searchParams.get('sex')==='female';
$('#sex').value=female?'female':'male';$('#sex').onchange=()=>{const url=new URL(location.href);url.searchParams.set('sex',$('#sex').value);location.href=url;};
const [mesh,look]=await Promise.all([female?'windstep-female.json':'windstep.json','windstep-look.json'].map(async file=>{const response=await fetch('out/'+file);if(!response.ok)throw Error(file+' unavailable');return response.json();}));
const audioData=await fetch('out/audio-plans.json').then(r=>r.json()),sound=new SoundPlayer(audioData,$('#audio'),$('#sound'),$('#soundStatus'));
const root=new THREE.Group();scene.add(root);
for(const n of mesh.nodes){const group=new THREE.Group();group.position.fromArray(n.translate);nodes[n.name]=group;if(n.billboard)billboards.push(group);}
for(const n of mesh.nodes)(nodes[n.parent]||root).add(nodes[n.name]);
for(const s of mesh.shapes){
 if(!textures.has(s.diffuse)){const t=await loader.loadAsync('out/'+s.diffuse);t.wrapS=t.wrapT=THREE.RepeatWrapping;textures.set(s.diffuse,t);}
 const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(s.positions,3));g.setAttribute('normal',new THREE.Float32BufferAttribute(s.normals,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(s.uvs,2));g.setAttribute('shade',new THREE.Float32BufferAttribute(s.colors,4));g.setIndex(s.indices);
 const material=new THREE.ShaderMaterial({uniforms:{map:{value:textures.get(s.diffuse)},tint:{value:new THREE.Vector3(1,.3,.05)},power:{value:0},drift:{value:0}},vertexShader:'attribute vec4 shade;varying vec2 vUv;varying vec4 vShade;void main(){vUv=uv;vShade=shade;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform vec3 tint;uniform float power;uniform float drift;varying vec2 vUv;varying vec4 vShade;void main(){vec4 t=texture2D(map,vUv+vec2(0.,drift));gl_FragColor=vec4(t.rgb*tint*vShade.rgb*power,t.a*vShade.a);}',transparent:true,depthWrite:false,side:s.doubleSided?THREE.DoubleSide:THREE.FrontSide,blending:THREE.AdditiveBlending});
 material.userData.scale=s.effect.scale;material.userData.drifting=/CloudTile/i.test(s.source);materials.push(material);
 if(s.name.startsWith('Echo0_')){
  const reference=new THREE.Mesh(g,new THREE.MeshStandardMaterial({map:textures.get(s.diffuse),color:0x657485,roughness:1}));figure.add(reference);
 }
 const object=new THREE.Mesh(g,material);object.position.fromArray(s.transform.translate);nodes[s.node].add(object);
}
const parts=look.names.map(name=>({node:nodes[name],materials:[]}));for(const part of parts)part.node.traverse(o=>{if(o.material)part.materials.push(o.material);});
function setRank(value){rank=Math.max(1,Math.min(3,value));document.querySelectorAll('[data-rank]').forEach(b=>b.setAttribute('aria-pressed',Number(b.dataset.rank)===rank));$('#subtitle').textContent=rank===3?'Tempest echo - a scattering gust':'Windstep - spectral echo and flowing air';elapsed=0;paused=false;$('#timeline').max=3+rank+.9;sound.load([...audioData.plans.windstep[rank-1],...(rank===3?audioData.plans['windstep-burst'][2].map(c=>({...c,at:c.at+3+rank})):[])],3+rank+.9);$('#pause').textContent='Pause';history.replaceState(null,'','?rank='+rank+'&sex='+(female?'female':'male'));}
document.querySelectorAll('[data-rank]').forEach(b=>b.onclick=()=>setRank(Number(b.dataset.rank)));setRank(rank);
$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Play':'Pause';};$('#replay').onclick=()=>{elapsed=0;paused=false;sound.reset();sound.unlock();$('#pause').textContent='Pause';};
$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;$('#pause').textContent='Play';};
$('#day').onchange=()=>{const day=$('#day').checked;scene.background.set(day?'#8993a2':'#14171c');ambient.intensity=day?3:1.8;sun.intensity=day?3.5:2;};
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min((now-last)/1000,.1);last=now;if(!paused){const before=elapsed;elapsed=(elapsed+dt)%(3+rank+.9+1);if(elapsed<before)sound.reset();}sound.update(elapsed,!paused,1);
 const t=Math.min(elapsed,3+rank+.9),frames=look.ranks[rank-1],at=Math.min(t*look.fps,frames.length-1),lo=Math.floor(at),f=at-lo,a=frames[lo],b=frames[Math.min(lo+1,frames.length-1)];
 const u=Math.min(1,t/.65),travel=u*u*(3-2*u);figure.position.y=-(175+35*rank)*travel;
 parts.forEach((part,i)=>{const p=a.parts[i].map((v,j)=>v+(b.parts[i][j]-v)*f);part.node.position.set(p[0],p[1],p[2]);part.node.scale.setScalar(p[3]);part.node.rotation.set(p[5],0,p[4],'ZYX');part.node.visible=p[6]>.0001;for(const mat of part.materials){mat.uniforms.tint.value.set(p[7],p[8],p[9]);mat.uniforms.power.value=p[6]*mat.userData.scale;mat.uniforms.drift.value=mat.userData.drifting?p[10]:0;}});
 root.updateMatrixWorld(true);for(const group of billboards){const q=new THREE.Quaternion();group.parent.getWorldQuaternion(q);group.quaternion.copy(q.invert().multiply(camera.quaternion));}
 glow.position.set(0,0,85);glow.color.setRGB(.6,1,.72);glow.intensity=(a.light+(b.light-a.light)*f)*1100;
 $('#timeline').value=t;$('#phase').textContent=(t<.65?'Retreat':t<3+rank?'Echo holds':t<3+rank+.6&&rank===3?'Scattering gust':t<3+rank+.9?'Dissipating':'Ended')+' · '+t.toFixed(2)+' s';controls.update();renderer.render(scene,camera);
}draw();
