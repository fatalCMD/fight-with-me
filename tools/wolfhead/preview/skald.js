import {createNativeLjodh,nativeLabels} from './skald-native.js';
import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {bardMote,hitMote,moteColor,rankGlow,rankSpread,rankMote,bardCount,hitCount,waveMote,luteVeil} from './skald-particles.js';
const $=s=>document.querySelector(s),query=new URLSearchParams(location.search);
document.body.classList.toggle('capture',query.has('capture'));
const data=await fetch('out/skald.json').then(r=>r.json());
const names=['Ljodh of Fervor','Ljodh of Echoes','Ljodh of the Hollow','Ljodh of Glory'];
const copy=[['attack speed','Spellweave · shorter casting wind-up','Relentless Rhythm · cheaper power attacks'],['ability cooldown recovery','Encore · immediate cooldown reduction','Inspired Casting · cheaper next ability'],['enemy damage','Leadfoot Verse · slower enemies','Broken Concentration · magicka drain'],['pp critical damage','Keen Chorus · increased critical chance','Victorious Verse · resource restoration']];
const thresholds=[3,5,10],contributions=[1,.5,.5],waveLife=data.wave.reach/data.wave.speed;
const audioTrack=()=>data.tracks[$('#song').value==='all'?0:Number($('#song').value)];
const cycleDuration=()=>Math.max(...($('#song').value==='all'?data.tracks:[audioTrack()]).map(t=>Math.max(t.seconds+waveLife,t.waves.at(-1)+Math.max(0,Number($('#distance').value)-data.wave.contact)/data.wave.speed+data.particles.hitSeconds)));
const hitAlpha=age=>age<0||age>=data.wave.hitSeconds?0:smooth(age/.045)*(1-smooth((age-.13)/(data.wave.hitSeconds-.13)));
let replaySeed=1;
const roll=(song,stack)=>{let h=(Math.imul((song+1)*31+(stack+1)*139+replaySeed*577,747796405)+2891336453)>>>0;h=Math.imul((h>>>((h>>>28)+4))^h,277803737)>>>0;return (((h>>>22)^h)>>>0)/4294967296;};
const smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
const hex=['#de945b','#80cbd1','#a187d4','#e0877e'];
const loader=new THREE.TextureLoader(),textures=new Map();
for(const mesh of [...data.meshes,data.lute,...data.hits])for(const s of mesh.shapes)if(!textures.has(s.texture)){
 const tex=await loader.loadAsync('out/'+s.texture);tex.wrapS=tex.wrapT=THREE.RepeatWrapping;if(s.shader.painted)tex.flipY=false;textures.set(s.texture,tex);
}
function meshOf(asset,color){
 const group=new THREE.Group(),materials=[],objects=new Map();
 for(const shape of asset.shapes){
  const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(shape.positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(shape.uvs,2));g.setAttribute('shade',new THREE.Float32BufferAttribute(shape.colors,4));g.setIndex(shape.indices);
  const material=new THREE.ShaderMaterial({uniforms:{map:{value:textures.get(shape.texture)},tint:{value:new THREE.Vector3(...color)},power:{value:1},uvScale:{value:new THREE.Vector2(...shape.shader.uvScale)},uvOffset:{value:new THREE.Vector2(...shape.shader.uvOffset)}},vertexShader:'attribute vec4 shade;varying vec2 vUv;varying vec4 vShade;void main(){vUv=uv;vShade=shade;gl_Position=projectionMatrix*modelViewMatrix*vec4(position,1.);}',fragmentShader:'uniform sampler2D map;uniform vec3 tint;uniform float power;uniform vec2 uvScale;uniform vec2 uvOffset;varying vec2 vUv;varying vec4 vShade;void main(){vec4 t=texture2D(map,vUv*uvScale+uvOffset);gl_FragColor=vec4(t.rgb*tint*vShade.rgb*power,t.a*vShade.a);}',transparent:true,depthWrite:false,side:THREE.DoubleSide,blending:THREE.AdditiveBlending});
  material.userData.scale=shape.shader.scale;material.userData.painted=shape.shader.painted;if(shape.shader.painted)material.uniforms.tint.value.set(1,1,1);materials.push(material);
  const object=new THREE.Mesh(g,material);object.position.fromArray(shape.transform.translate);object.scale.setScalar(shape.transform.scale);group.add(object);objects.set(shape.name,{object,material});
 }
 return {group,materials,objects};
}
function figure(performer=true){
 const group=new THREE.Group(),cloth=new THREE.MeshStandardMaterial({color:0x42464a,roughness:1}),skin=new THREE.MeshStandardMaterial({color:0x716b62,roughness:1});
 function segment(a,b,r,mat=cloth){const start=new THREE.Vector3(...a),end=new THREE.Vector3(...b),v=end.clone().sub(start),mesh=new THREE.Mesh(new THREE.CylinderGeometry(r,r,1,14),mat);mesh.position.copy(start.add(end).multiplyScalar(.5));mesh.scale.y=v.length();mesh.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),v.normalize());group.add(mesh);return mesh;}
 function ball(at,size,material){const obj=new THREE.Mesh(new THREE.SphereGeometry(size,20,14),material);obj.position.fromArray(at);group.add(obj);return obj;}
 segment([-10,0,10],[-9,0,75],7);segment([10,0,10],[9,0,75],7);
 const torso=ball([0,0,101],20,cloth);torso.scale.set(1,.6,1.6);
 ball([0,0,146],12,skin);
 if(performer){
  segment([-19,0,119],[-29,13,96],6);const arm=segment([-29,13,96],[-9,28,96],5,skin);
  segment([19,0,119],[28,9,106],6);segment([28,9,106],[25,26,123],5,skin);
  const hand=ball([-9,28,96],5,skin);ball([25,26,123],5,skin);
  group.userData.play=(time,bpm,playing)=>{
   const swing=playing?Math.sin(time*bpm/60*Math.PI*2):0;
   const start=new THREE.Vector3(-29,13,96),end=new THREE.Vector3(-9+swing*2,28,96+swing*9),v=end.clone().sub(start);
   arm.position.copy(start.add(end).multiplyScalar(.5));arm.scale.y=v.length();arm.quaternion.setFromUnitVectors(new THREE.Vector3(0,1,0),v.normalize());hand.position.copy(end);
   torso.rotation.y=playing?Math.sin(time*bpm/60*Math.PI)*.025:0;
  };
 }
 else{segment([-19,0,119],[-23,0,76],6);segment([19,0,119],[23,0,76],6);}
 return group;
}
function placeMote(look,frame,camera,color,zOffset){
 look.object.visible=frame.alpha>.001;if(!look.object.visible)return;
 look.object.position.set(frame.x,frame.y,frame.z-zOffset);look.object.scale.setScalar(frame.scale);
 look.object.quaternion.copy(camera.quaternion);look.material.uniforms.power.value=frame.alpha*look.material.userData.scale;
 look.material.uniforms.tint.value.fromArray(color);
}
let elapsed=query.has('time')?Number(query.get('time')):query.has('peak')?6.65:0,paused=query.has('still'),last=performance.now(),panels=[],audio=null;
if(query.has('song'))$('#song').value=query.get('song');
if(query.has('rank'))$('#rank').value=query.get('rank');
$('#detail').checked=query.has('detail');
$('#bard').checked=query.has('bard');
function build(){
 for(const p of panels){p.controls.dispose();p.renderer.dispose();p.scene.traverse(o=>{if(o.geometry)o.geometry.dispose();if(o.material)o.material.dispose();});}
 panels=[];$('#grid').innerHTML='';const selection=$('#song').value;document.body.classList.toggle('single',selection!=='all');
 for(let i=0;i<4;i++){
  if(selection!=='all'&&Number(selection)!==i)continue;
  const card=document.createElement('article');card.innerHTML=`<div class="viewport"></div><div class="label"><small style="color:${hex[i]}">LJODH ${['I','II','III','IV'][i]}</small><h2>${names[i]}</h2></div><div class="rank">${data.tracks[i].bpm} BPM · RANK III</div><div class="meter"><span class="hits"></span><div class="stacks">${thresholds.map((n,j)=>`<span class="stack" title="${n} total waves">${['I','II','III'][j]}</span>`).join('')}</div><span class="factor"></span><div class="hit-state">Wave received</div></div><div class="copy"><strong></strong><p>${copy[i][1]}<br>${copy[i][2]}</p></div>`;$('#grid').append(card);
  const types=document.createElement('div');types.className='particle-types';types.textContent=nativeLabels[i];card.querySelector('.label').append(types);
  const host=card.querySelector('.viewport'),scene=new THREE.Scene();scene.background=new THREE.Color('#11151a');
  const renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,2));host.append(renderer.domElement);
  const camera=new THREE.PerspectiveCamera(40,1,.1,4000);camera.up.set(0,0,1);camera.position.set(350,760,740);
  const controls=new OrbitControls(camera,renderer.domElement);controls.target.set(0,0,45);controls.enableDamping=true;controls.enablePan=false;controls.update();
  if($('#detail').checked){const x=Number($('#distance').value)/2;camera.position.set(x+165,300,255);controls.target.set(x,0,64);controls.update();}
  if($('#bard').checked){const x=-Number($('#distance').value)/2;camera.position.set(x+170,300,245);controls.target.set(x,0,76);controls.update();}
  if(query.has('capture')){camera.position.set(120,500,305);controls.target.set(0,0,66);controls.update();}
  scene.add(new THREE.HemisphereLight(0xd4d5d7,0x24232b,2));const light=new THREE.DirectionalLight(0xffe5c9,2.2);light.position.set(-120,180,300);scene.add(light);
  const floor=new THREE.Mesh(new THREE.CircleGeometry(1250,128),new THREE.MeshStandardMaterial({color:0x20252b,roughness:1}));floor.position.z=-1;scene.add(floor);
  const person=figure();scene.add(person);const recipient=figure(false);scene.add(recipient);
  const effect=meshOf({shapes:[]},data.colors[i]);scene.add(effect.group);
  const lute=meshOf(data.lute,data.colors[i]);lute.group.rotation.z=-.38;lute.group.position.set(5,12,102);scene.add(lute.group);
  const foot=meshOf({shapes:[]},data.colors[i]);scene.add(foot.group);
  const footLight=new THREE.PointLight(new THREE.Color(...data.colors[i]),0,115,2);scene.add(footLight);
  const bodyLight=new THREE.PointLight(new THREE.Color(...data.colors[i]),0,145,2);scene.add(bodyLight);
  const glow=new THREE.PointLight(new THREE.Color(...data.colors[i]),250,270,2);glow.position.set(0,45,95);scene.add(glow);
  const resize=()=>{renderer.setSize(host.clientWidth,host.clientHeight);camera.aspect=host.clientWidth/host.clientHeight;camera.updateProjectionMatrix();};resize();
  const native=createNativeLjodh(scene,i);effect.group.visible=!native;
  panels.push({native,i,card,scene,renderer,camera,controls,effect,lute,glow,foot,footLight,bodyLight,person,recipient,resize});
 }
 elapsed=Math.min(elapsed,cycleDuration());$('#timeline').max=cycleDuration();loadAudio();
}
function loadAudio(){if(audio){audio.pause();audio.remove();audio=null;}if(!$('#sound').checked)return;const i=$('#song').value==='all'?0:Number($('#song').value);audio=new Audio('out/skald-'+i+'.wav');audio.id='skald-audio';audio.hidden=true;document.body.append(audio);audio.volume=.75;audio.currentTime=Math.min(elapsed,audioTrack().seconds);if(!paused&&elapsed<audioTrack().seconds)audio.play().catch(()=>{$('#status').textContent='Press Replay to start music';});}
$('#song').onchange=build;$('#rank').onchange=build;$('#sound').onchange=loadAudio;
$('#detail').onchange=()=>{if($('#detail').checked)$('#bard').checked=false;build();};
$('#bard').onchange=()=>{if($('#bard').checked)$('#detail').checked=false;build();};
$('#distance').oninput=()=>{$('#timeline').max=cycleDuration();};
$('#pause').onclick=()=>{if(paused&&elapsed>=cycleDuration()){elapsed=0;if(audio)audio.currentTime=0;}paused=!paused;$('#pause').textContent=paused?'Play':'Pause';if(audio){if(paused)audio.pause();else if(elapsed<audioTrack().seconds)audio.play();}};
$('#replay').onclick=()=>{elapsed=0;replaySeed++;paused=false;$('#pause').textContent='Pause';if(audio){audio.currentTime=0;audio.play();}};
$('#timeline').oninput=()=>{elapsed=Number($('#timeline').value);paused=true;$('#pause').textContent='Play';if(audio){audio.pause();audio.currentTime=elapsed;}};
addEventListener('resize',()=>panels.forEach(p=>p.resize()));build();$('#pause').textContent=paused?'Play':'Pause';
function draw(){requestAnimationFrame(draw);const tick=performance.now(),dt=Math.min((tick-last)/1000,.1);last=tick;if(!paused){elapsed=audio&&elapsed<audioTrack().seconds?(audio.readyState>=2&&!audio.seeking&&!audio.paused?audio.currentTime:elapsed):elapsed+dt;if(elapsed>=audioTrack().seconds&&audio&&!audio.paused)audio.pause();if(elapsed>cycleDuration()){elapsed=cycleDuration();paused=true;$('#pause').textContent='Play';if(audio)audio.pause();}}
 const distance=Number($('#distance').value),rank=Number($('#rank').value),spread=rankSpread(rank),strength=rankGlow(rank);$('#distance-value').textContent=distance+' units';
 for(const p of panels){const track=data.tracks[p.i],beats=track.beats,waves=track.waves,sample=elapsed,lastBeat=beats.findLast(b=>b<=sample),finished=sample>=track.seconds;
  p.card.querySelector('.rank').textContent=track.bpm+' BPM · RANK '+['I','II','III'][rank-1];
  const accent=lastBeat===undefined?0:Math.exp(-Math.max(0,sample-lastBeat)*10),origin=-distance/2;
  p.person.position.x=origin;p.recipient.position.x=distance/2;p.effect.group.position.set(origin,0,3);p.lute.group.position.set(origin+5,12,102);p.glow.position.x=origin;
  p.lute.group.visible=!finished;p.person.userData.play(sample,track.bpm,!finished);
  let bardParticles=0;
  for(let i=0;!p.native&&i<data.particles.bard;i++){
   const frame=rankMote(bardMote(p.i,i,finished||i>=bardCount(rank)?-1:sample,lastBeat===undefined?-1:sample-lastBeat),rank);
   placeMote(p.effect.objects.get('BardMoteGlow'+i),frame,p.camera,moteColor(p.i,i,data.colors[p.i]),3);
   if(frame.alpha>.001)bardParticles++;
  }
  p.card.dataset.bardParticles=bardParticles;
  const active=waves.filter(b=>sample>=b&&sample-b<waveLife);p.card.dataset.groundWaves=active.length;
  let waveParticles=0;
  for(let slot=0;!p.native&&slot<data.wave.slots;slot++){
   const born=active[slot];
   for(let i=0;i<data.particles.wave;i++){
    const frame=waveMote(p.i,i,born===undefined?-1:sample-born,rank,data.wave);
    placeMote(p.effect.objects.get('WaveMoteGlow'+(slot*data.particles.wave+i)),frame,p.camera,moteColor(p.i,i,data.colors[p.i]),3);
    if(frame.alpha>.001)waveParticles++;
   }

  }
  p.card.dataset.waveParticles=waveParticles;
  const delay=Math.max(0,distance-data.wave.contact)/data.wave.speed,arrivals=waves.map(b=>b+delay);
  const hits=Math.min(10,arrivals.filter(t=>sample>=t).length),lastHit=arrivals.findLast(t=>t<=sample),hitAge=lastHit===undefined?-1:sample-lastHit,alpha=hitAlpha(hitAge);
  p.foot.group.position.set(distance/2,0,2);p.foot.group.visible=!p.native&&hitAge>=0&&hitAge<data.particles.hitSeconds;
  let particleLight=0,hitParticles=0;
  for(let slot=0;!p.native&&slot<data.particles.bursts;slot++){
   const born=arrivals.filter((_,i)=>i%data.particles.bursts===slot).findLast(t=>t<=sample);
   for(let i=0;i<data.particles.hit;i++){
    const frame=rankMote(hitMote(p.i,i,born===undefined||i>=hitCount(rank)?-1:sample-born),rank);
    placeMote(p.foot.objects.get('HitMoteGlow'+(slot*data.particles.hit+i)),frame,p.camera,moteColor(p.i,i,data.colors[p.i]),2);
    particleLight=Math.max(particleLight,frame.alpha);if(frame.alpha>.001)hitParticles++;
   }
  }
  if(p.native){const counts=p.native.update(sample,rank,track,origin,distance,delay,p.camera);p.card.dataset.bardParticles=counts.Aura;p.card.dataset.waveParticles=counts.Wave;hitParticles=counts.Hit;particleLight=hitAge<0?0:Math.max(0,1-hitAge/1.4)*strength;}
  p.card.dataset.hitParticles=hitParticles;p.bodyLight.position.set(distance/2,0,75);p.bodyLight.intensity=particleLight*220;
  p.footLight.position.set(distance/2,0,14);p.footLight.intensity=alpha*650;p.card.dataset.hitLight=alpha.toFixed(3);
  p.card.querySelector('.hit-state').hidden=alpha<=0;p.card.querySelector('.hit-state').style.opacity=.5+alpha*.5;
  let factor=0,stacks=0,gold=false;
  thresholds.forEach((threshold,j)=>{const earned=hits>=threshold,boosted=earned&&$('#empowered').checked&&roll(p.i,j)<.15;if(earned){stacks++;factor+=contributions[j]*(boosted?2:1);}p.card.querySelectorAll('.stack')[j].className='stack'+(earned?' earned':'')+(boosted?' gold':'');const earnedAt=arrivals[threshold-1];if(boosted&&sample-earnedAt<.45)gold=true;});
  p.card.querySelector('.hits').textContent=(p.i===2?'Enemy':'Ally')+' · '+hits+' wave hits';p.card.querySelector('.factor').textContent=stacks+' / 3 stacks · '+factor.toFixed(1)+'× strength';
  p.card.querySelector('.copy strong').textContent=(p.i===2?'−':'+')+([[10,25,10,25],[15,40,15,40],[20,60,20,60]][rank-1][p.i]*factor).toFixed(0)+(p.i===3?' ':'% ')+copy[p.i][0];
  const spectral=luteVeil(sample,accent),tint=(gold?[1,.78,.26]:data.colors[p.i]).map(c=>.25+.75*c);
  const body=p.lute.objects.get('SpectralStrings'),veil=p.lute.objects.get('SpectralVeil');
  body.material.uniforms.power.value=(1.1+accent*.45)*body.material.userData.scale;body.material.uniforms.tint.value.fromArray(tint);
  body.material.uniforms.uvOffset.value.y=sample*.035;
  veil.object.position.set(spectral.x,spectral.y,spectral.z);veil.object.rotation.set(spectral.angle*.5,spectral.angle,0);veil.object.scale.setScalar(spectral.scale);
  veil.material.uniforms.power.value=spectral.glow*veil.material.userData.scale;veil.material.uniforms.tint.value.fromArray(tint);veil.material.uniforms.uvOffset.value.y=spectral.scroll;
  p.glow.intensity=finished?0:(320+accent*280)*strength;p.glow.distance=300;
  p.controls.update();p.renderer.render(p.scene,p.camera);
 }
 $('#timeline').value=elapsed;$('#time').textContent=elapsed.toFixed(2)+' s';$('#status').textContent=paused?'Paused':elapsed>=audioTrack().seconds?'Final waves · support stance resumed':$('#sound').checked?audioTrack().label+($('#song').value==='all'?' · Fervor audio only':''):'One wave every two beats';
}draw();
