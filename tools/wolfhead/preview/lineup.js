import * as THREE from 'three';
import {OrbitControls} from 'three/addons/controls/OrbitControls.js';
import {materialsFor} from './lineup-materials.js';
const $=id=>document.getElementById(id),query=new URLSearchParams(location.search);
const compact=query.get('size')!=='original';$('size').value=compact?'compact':'original';$('rank').value=query.get('rank')||'1';$('element').value=query.get('element')||'fire';
const renderer=new THREE.WebGLRenderer({antialias:true,preserveDrawingBuffer:true});renderer.setPixelRatio(Math.min(devicePixelRatio,1.5));renderer.setSize(innerWidth,innerHeight);document.body.append(renderer.domElement);
const {glowMaterial,geometryOf}=materialsFor(renderer),cache=new Map(),stages=[];
const fetchJSON=async name=>{const r=await fetch('out/'+name+'.json');if(!r.ok)throw Error(name+' could not load');return r.json();};
const matrix=r=>new THREE.Matrix4().set(r[0],r[1],r[2],0,r[3],r[4],r[5],0,r[6],r[7],r[8],0,0,0,0,1);
function turn(node,r){node.setRotationFromMatrix(matrix(r));}
function paint(node,color,alpha){node?.traverse(o=>{if(o.material?.uniforms){const u=o.material.uniforms;u.opacity.value=alpha;u.tint.value.set(...color,1);}});}
async function model(stage,file){
 if(stage.models.has(file))return stage.models.get(file);
 if(!cache.has(file))cache.set(file,fetchJSON(file));const data=await cache.get(file),top=new THREE.Group(),nodes={},billboards=[];
 for(const n of data.nodes){const o=new THREE.Group();o.position.fromArray(n.translate);o.userData.rest=o.position.clone();o.scale.setScalar(n.scale??1);if(n.rotate)turn(o,n.rotate);nodes[n.name]=o;if(n.billboard)billboards.push(o);}
 for(const n of data.nodes)(nodes[n.parent]||top).add(nodes[n.name]);
 for(const s of data.shapes){const material=s.effect?glowMaterial(s):new THREE.MeshStandardMaterial({color:0xb8ccdf,transparent:true,opacity:.65});const mesh=new THREE.Mesh(geometryOf(s),material);mesh.position.fromArray(s.transform.translate);mesh.scale.setScalar(s.transform.scale);turn(mesh,s.transform.rotate);nodes[s.node].add(mesh);}
 top.visible=false;stage.scene.add(top);const m={top,nodes,billboards};stage.models.set(file,m);return m;
}
const configs=[['dragon','Dragon’s Breath','dragon-breath.html','Head and beam directly in front of the mage.'],['wolf','Kynareth’s Maw','index.html?scene=wolf','Bite remains centred on the target.'],['bear','Taunt','index.html?scene=taunt','Smaller bear; crimson at rank III.'],['sabre','War Cry','index.html?scene=warcry','Golden sabre-cat; larger crimson head at rank III.'],['moon','Azura’s Moon','azura.html','Smaller apparition; blessing radius is unchanged.'],['castle','Last Bastion','index.html?scene=bastion','Compact landing; guardian ward remains on the tank.'],['rain','Arrow Rain','index.html?scene=rain','Forward portal · 24 / 36 / 48 arrows · 1.5 seconds.']];
for(const [id,title,url,caption]of configs){const el=document.createElement('article');el.innerHTML=`<h2>${title}</h2><a target="_blank">Open ↗</a><p>${caption}</p>`;$('cards').append(el);const scene=new THREE.Scene();scene.background=new THREE.Color('#141a22');scene.add(new THREE.HemisphereLight(0xd6e3ff,0x303449,2));const camera=new THREE.PerspectiveCamera(43,1,1,6000);camera.up.set(0,0,1);const controls=new OrbitControls(camera,el);controls.enableDamping=true;controls.enablePan=true;
 const stream=id==='dragon'||id==='rain';camera.position.set(...(stream?[700,-650,420]:[370,600,300]));controls.target.set(0,stream?280:0,stream?95:105);controls.update();
 const grid=new THREE.GridHelper(stream?1800:1000,stream?36:20,0x38434d,0x222c35);grid.rotation.x=Math.PI/2;scene.add(grid);
 const human=new THREE.Group(),mat=new THREE.MeshStandardMaterial({color:0x8997a7,transparent:true,opacity:.65});
 const torso=new THREE.Mesh(new THREE.CapsuleGeometry(13,45,6,10),mat);torso.rotation.x=Math.PI/2;torso.position.z=82;human.add(torso);const head=new THREE.Mesh(new THREE.SphereGeometry(12,16,12),mat);head.position.z=128;human.add(head);
 for(const x of [-10,10]){const leg=new THREE.Mesh(new THREE.CylinderGeometry(6,6,48,8),mat);leg.rotation.x=Math.PI/2;leg.position.set(x,0,25);human.add(leg);}scene.add(human);
 stages.push({id,el,url,scene,camera,controls,models:new Map(),human});
}
const suffix=compact?'-compact':'',looks={};
const sample=(frames,t,fps=60)=>frames[Math.min(frames.length-1,Math.max(0,Math.floor(t*fps)))];
let rank=+$('rank').value,element=$('element').value,age=0,last=performance.now(),paused=false,ready=false;
async function prepare(){ready=false;rank=+$('rank').value;element=$('element').value;const grand=rank===3;
 for(const s of stages){s.el.querySelector('a').href=s.url+(s.url.includes('?')?'&':'?')+`rank=${rank}&size=${compact?'compact':'original'}&element=${element}`;
 const files=s.id==='dragon'?['breath-'+(grand?'alduin':'head'),`breath-${element}-${rank}${suffix}`]:s.id==='wolf'?[grand?'head-red':'head']:s.id==='bear'?[grand?'bear-red':'bear']:s.id==='sabre'?[grand?'warcry-sabre-red':'warcry-sabre']:s.id==='moon'?['azura']:s.id==='castle'?[grand?'bastion-grand':'bastion']:['rain'];
 for(const m of s.models.values())m.top.visible=false;s.current=await Promise.all(files.map(f=>model(s,f)));for(const m of s.current)m.top.visible=true;
 }age=0;ready=true;$('status').textContent='Drag any panel to orbit; scroll inside it to zoom. Actual shipped meshes and native animation samples; Skyrim lighting may differ. Fire uses Skyrim’s native shout particles in-game; browser flames approximate them. Circles and Magnus retain their current size.';}
function beast(s,t){const grand=rank===3,look=looks.bear,frames=s.id==='sabre'?(grand?look.warGrand:look.warFrames):(grand?look.grand:look.frames),p=sample(frames,t%4.7),m=s.current[0];const c=Math.cos(p.yaw),si=Math.sin(p.yaw),cp=Math.cos(p.pitch),sp=Math.sin(p.pitch),h=m.nodes.Head;
 h.position.set(p.side,p.forward,p.height);h.scale.setScalar(p.s);turn(h,[c,si*cp,-si*sp,-si,c*cp,-c*sp,0,sp,cp]);m.nodes.Jaw.rotation.x=-p.jaw;paint(h,p.tint,p.a);
 p.waves.forEach(([forward,radius,alpha],i)=>{const n=m.nodes['Wave'+i];n.position.set(0,forward,compact||s.id==='sabre'?p.height:205);n.scale.setScalar(Math.max(radius,.001));paint(n,p.glow,alpha);});}
function dragon(s,t){const run=looks.dragon.ranks[rank-1],p=sample(run.frames,t%(run.seconds+.7)),[head,flow]=s.current,h=head.nodes.Head;
 h.position.set(0,p.forward,p.height);h.scale.setScalar(p.s);h.rotation.x=p.pitch;head.nodes.Jaw.rotation.x=p.jaw;paint(h,rank===3?[.9,.13,.1]:[.65,.85,1],p.a);
 const rune=head.nodes.SummoningRune;rune.position.set(0,15,p.runeRadius+12);rune.scale.setScalar(p.runeRadius);rune.rotation.y=p.runeAngle;paint(rune,[1,1,1],p.runeAlpha);
 const mouth=new THREE.Vector3(0,p.forward+p.muzzleY*p.s,p.height+p.muzzleZ*p.s);flow.nodes.Stream.position.copy(mouth);flow.nodes.Stream.scale.setScalar(Math.max(run.reach*p.front/1000,.001));
 const color=element==='fire'?[1,.28,.035]:element==='frost'?[.58,.88,1]:[.45,.42,1],ease=x=>{x=Math.min(1,Math.max(0,x));return x*x*(3-2*x);};
 for(let i=0;i<8;i++){const n=flow.nodes['Jet'+i];n.rotation.y=.1*Math.sin(t*6-i*.7);paint(n,rank===3&&element==='fire'&&i<3?[.06,.40,1]:color,p.stream*(element==='shock'?.35+.65*Math.abs(Math.sin(t*39+i*1.7)):.8+.2*Math.sin(t*11+i)));n.traverse(o=>{if(o.material?.uniforms)o.material.uniforms.uvOffset.value.y=-t*(element==='fire'?3.5:1.5)+i*.17;});}
 for(let i=0;i<32;i++){const f=(t*1.25+i/32)%1,a=i*2.399+t,r=run.radius*1000/run.reach*(.16+.84*ease(f/.12))*(.15+.55*(i%7)/6),n=flow.nodes['Puff'+i];n.position.set(Math.cos(a)*r,1000*f,Math.sin(a)*r);paint(n,color,p.stream*Math.sin(f*Math.PI)*.65);}
 if(element==='shock')for(let i=0;i<64;i++){const [angle,x,z,alpha]=p.arcs[i],n=flow.nodes['Arc'+i];n.rotation.y=angle;n.position.copy(n.userData.rest).add(new THREE.Vector3(x,0,z));paint(n,color,p.stream*alpha);}
 if(rank===3&&element==='frost')for(let i=0;i<16;i++){const f=(t*1.65+i/16)%1,a=i*2.399+t*2,r=run.radius*1000/run.reach*.72,n=flow.nodes['Ice'+i];n.position.set(Math.cos(a)*r,1000*f,Math.sin(a)*r);paint(n,color,p.stream*Math.sin(f*Math.PI));}
 if(rank===3&&element==='shock')for(let i=0;i<4;i++){const n=flow.nodes['Ground'+i],[alpha,fraction,side]=p.ground[i],from=mouth.clone().add(new THREE.Vector3(0,run.reach*fraction,0)),to=from.clone().add(new THREE.Vector3(run.radius*side,0,-from.z)),d=to.sub(from),len=d.length();d.normalize();const right=new THREE.Vector3().crossVectors(d,new THREE.Vector3(0,0,1)).normalize(),up=new THREE.Vector3().crossVectors(right,d);n.position.copy(from);n.setRotationFromMatrix(new THREE.Matrix4().makeBasis(right,d,up));n.scale.setScalar(len/100);paint(n,[.7,.72,1],alpha);}}
function animate(s,t){const m=s.current[0],n=m.nodes;
 if(s.id==='dragon')return dragon(s,t);
 if(s.id==='bear'||s.id==='sabre')return beast(s,t);
 if(s.id==='wolf'){const run=rank===3?looks.wolf.grand:looks.wolf.plain,p=sample(run.frames,t%(run.gone+.8));n.Head.position.fromArray(p.p);n.Head.scale.setScalar(p.s);turn(n.Head,p.r);n.Jaw.rotation.x=-p.jaw;paint(n.Head,[1,1,1],p.a);return;}
 if(s.id==='moon'){const f=sample(looks.moon.ranks[rank-1],t%8);looks.moon.names.forEach((name,i)=>{const p=f.parts[i],o=n[name];o.position.fromArray(p.slice(0,3));o.scale.setScalar(Math.max(p[3],.001));o.rotation.set(-p[5],0,-p[4],'ZYX');paint(o,p.slice(7,10),p[6]);});return;}
 if(s.id==='castle'){const f=sample(looks.castle.ranks[rank-1],t%5);looks.castle.names.forEach((name,i)=>{const p=f.parts[i],o=n[name];o.position.set(0,0,p[1]);o.scale.setScalar(Math.max(p[0],.001));o.rotation.z=-p[2];paint(o,p.slice(4,7),p[3]);});return;}
 if(s.id==='rain'){const run=rank===3?looks.rain.grand:rank===2?looks.rain.second:looks.rain.plain,f=sample(run.frames,t%(run.seconds+.5)),p=f.look,sc=(rank===3?85:65)/300;
 for(const key of ['Mark','Sky','Burst']){n[key].position.set(0,85,100);n[key].rotation.x=Math.PI/2;n[key].scale.setScalar(sc*(key==='Mark'?p[1]:key==='Sky'?p[9]:p[14]));}
 for(const [key,alpha,color]of [['Ring',p[0],looks.rain.markColor],['Dashes',p[0],looks.rain.markColor],['Heads',p[5],looks.rain.markColor],['Outer',p[6],looks.rain.markColor],['Floor',p[7],looks.rain.markColor],['Halo',p[8],looks.rain.skyColor],['Swirl',p[8],looks.rain.skyColor],['Crown',p[11],looks.rain.skyColor],['Burst',p[13],looks.rain.trailColor],['Flare',0,looks.rain.trailColor]])paint(n[key],color,alpha);
 n.Ring.rotation.z=p[2];n.Dashes.rotation.z=p[3];n.Swirl.rotation.z=p[10];
 for(let i=0;i<96;i++){const key=String(i).padStart(2,'0'),a=f.arrows[i],o=n['Arrow'+key],hit=n['Hit'+key];o.visible=!!a;hit.visible=!!a;if(!a)continue;o.position.fromArray(a.slice(0,3));const back=new THREE.Vector3(Math.cos(a[3])*Math.sin(a[4]),Math.sin(a[3])*Math.sin(a[4]),Math.cos(a[4]));o.quaternion.setFromUnitVectors(new THREE.Vector3(0,0,1),back);paint(n['Arrow'+key+'Shape'],looks.rain.trailColor,a[8]);paint(n['Arrow'+key+'Trail'],looks.rain.trailColor,a[6]);n['Arrow'+key+'Trail'].scale.setScalar(Math.max(a[5],.001));paint(n['Arrow'+key+'Glint'],looks.rain.trailColor,a[7]);hit.position.fromArray(a.slice(16,19));paint(hit,looks.rain.trailColor,a[10]);}
 }}
function draw(){requestAnimationFrame(draw);const now=performance.now(),dt=Math.min(.05,(now-last)/1000);last=now;if(!paused)age+=dt;if(!ready)return;renderer.setScissorTest(false);renderer.clear();renderer.setScissorTest(true);
 for(const s of stages){const rect=s.el.getBoundingClientRect();if(rect.bottom<0||rect.top>innerHeight)continue;animate(s,age);s.controls.update();s.scene.updateMatrixWorld(true);for(const m of s.current)for(const node of m.billboards){const q=new THREE.Quaternion();node.parent.getWorldQuaternion(q);node.quaternion.copy(q.invert().multiply(s.camera.quaternion));}
 s.camera.aspect=rect.width/rect.height;s.camera.updateProjectionMatrix();renderer.setViewport(rect.left,innerHeight-rect.bottom,rect.width,rect.height);renderer.setScissor(rect.left,innerHeight-rect.bottom,rect.width,rect.height);renderer.render(s.scene,s.camera);}}
try{await Promise.all([['dragon','breath-look'],['wolf','pose'],['bear','bear-look'],['moon','azura-look'],['castle','bastion-look'],['rain','rain-look']].map(async([key,file])=>looks[key]=await fetchJSON(file+(key==='rain'?'':suffix))));await prepare();draw();}catch(error){$('status').textContent=error.message;console.error(error);}
$('time').oninput=()=>{age=Number($('time').value);paused=true;$('pause').textContent='Play';};
$('size').onchange=()=>{query.set('size',$('size').value);query.set('rank',$('rank').value);query.set('element',$('element').value);location.search=query;};$('rank').onchange=$('element').onchange=()=>prepare().catch(e=>$('status').textContent=e.message);$('replay').onclick=()=>age=0;$('pause').onclick=()=>{paused=!paused;$('pause').textContent=paused?'Play':'Pause';};addEventListener('resize',()=>renderer.setSize(innerWidth,innerHeight));
