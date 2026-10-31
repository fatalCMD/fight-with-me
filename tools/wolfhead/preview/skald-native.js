// Art-direction approximation of native emitters; Skyrim owns the actual simulation and lighting.
import * as THREE from 'three';
const {specs,profiles}=await fetch('out/native-ljodhs.json').then(r=>r.json());
const loader=new THREE.TextureLoader();
const textureNames=['FXsmokePuffAnim04.dds','FXGlowSpotLinearAlpha.dds','MagicIceWisps.dds','FXSmokeNecroAnim.dds'];
const maps=new Map(await Promise.all(textureNames.map(async name=>{const t=await loader.loadAsync('out/native/'+name.toLowerCase().replace('.dds','.png'));t.flipY=false;t.wrapS=t.wrapT=THREE.ClampToEdgeWrapping;return [name,t];})));
export const nativeLabels=profiles.map(p=>p.label);
const random=n=>{let x=Math.imul(n+17,747796405)+2891336453;x=Math.imul((x>>>((x>>>28)+4))^x,277803737);return (((x>>>22)^x)>>>0)/4294967296;};
const smooth=x=>{x=Math.max(0,Math.min(1,x));return x*x*(3-2*x);};
export function createNativeLjodh(scene,song){
 const capacity=2048;
 const batches=specs.find(s=>s.songIndex===song&&s.kind==='Aura'&&s.rank===3).layers.map(layer=>{
  const map=maps.get(layer.texture);
  const plane=new THREE.PlaneGeometry(1,1),geometry=new THREE.InstancedBufferGeometry();geometry.index=plane.index;geometry.attributes.position=plane.attributes.position;geometry.attributes.uv=plane.attributes.uv;
  const offset=new THREE.InstancedBufferAttribute(new Float32Array(capacity*3),3),shape=new THREE.InstancedBufferAttribute(new Float32Array(capacity*4),4),tint=new THREE.InstancedBufferAttribute(new Float32Array(capacity*4),4);
  geometry.setAttribute('offset',offset);geometry.setAttribute('shape',shape);geometry.setAttribute('tint',tint);geometry.instanceCount=0;
  const material=new THREE.ShaderMaterial({uniforms:{map:{value:map},tiles:{value:new THREE.Vector2(layer.columns,layer.rows)},cameraRight:{value:new THREE.Vector3()},cameraUp:{value:new THREE.Vector3()}},transparent:true,depthWrite:false,blending:THREE.NormalBlending,
   vertexShader:'attribute vec3 offset;attribute vec4 shape;attribute vec4 tint;uniform vec2 tiles;varying vec2 vUv;varying vec4 vTint;varying float vHeight;uniform vec3 cameraRight;uniform vec3 cameraUp;void main(){float c=cos(shape.y),s=sin(shape.y);vec2 p=mat2(c,-s,s,c)*(position.xy*vec2(shape.w,1.))*shape.x*2.;vec4 mv=modelViewMatrix*vec4(offset,1.);mv.xy+=p;vHeight=(offset+cameraRight*p.x+cameraUp*p.y).z;gl_Position=projectionMatrix*mv;vec2 frame=vec2(mod(shape.z,tiles.x),floor(shape.z/tiles.x));vUv=(uv+frame)/tiles;vTint=tint;}',
   fragmentShader:'uniform sampler2D map;varying vec2 vUv;varying vec4 vTint;varying float vHeight;void main(){vec4 tex=texture2D(map,vUv);gl_FragColor=vec4(tex.rgb*vTint.rgb,tex.a*vTint.a*smoothstep(0.,18.,vHeight));}' });
  const mesh=new THREE.Mesh(geometry,material);mesh.frustumCulled=false;scene.add(mesh);return {mesh,geometry,offset,shape,tint,count:0};
 });
 return {update(time,rank,track,origin,distance,delay,camera){
  for(const b of batches){b.count=0;b.mesh.material.uniforms.cameraRight.value.setFromMatrixColumn(camera.matrixWorld,0);b.mesh.material.uniforms.cameraUp.value.setFromMatrixColumn(camera.matrixWorld,1);}
  const counts={Aura:0,Wave:0,Hit:0};
  for(const kind of ['Aura','Wave','Hit']){
   const spec=specs.find(s=>s.songIndex===song&&s.kind===kind&&s.rank===rank),events=kind==='Aura'?track.beats:track.waves;
   for(let event=0;event<events.length;event++){
    const age=time-events[event]-(kind==='Hit'?delay:0);if(age<0||age>spec.seconds)continue;
    spec.layers.forEach((p,layer)=>{
     const b=batches[layer];
     for(let i=0;i<Math.ceil(p.rate*p.emit);i++){
      const seed=event*7103+i*127+layer*1987+kind.length*613,born=(i+.5)/p.rate;
      const birthEnvelope=Math.min(1,born/.025,(p.emit-born)/(p.emit*.25));if(random(seed+3)>birthEnvelope)continue;
      const life=p.life+(random(seed+5)*2-1)*p.lifeVariation,t=age-born;if(t<0||t>=life||b.count>=capacity)continue;
      const u=t/life,a=random(seed+11)*Math.PI*2,r=Math.sqrt(random(seed+17))*p.radius,heading=random(seed+19)*Math.PI*2,decl=p.declination+(random(seed+23)*2-1)*p.spread;
      const speed=p.speed+(random(seed+29)*2-1)*p.speedVariation,xy=Math.sin(decl)*speed*t;
      const drift=Math.sin(t*3+seed)*p.turbulence*t*.13;
      const x=(kind==='Hit'?distance/2:origin)+Math.cos(a)*r+Math.cos(heading)*xy+drift,y=Math.sin(a)*r+Math.sin(heading)*xy-drift*.6,z=p.z+(random(seed+31)-.5)*p.height+Math.cos(decl)*speed*t+.5*p.force*t*t;
      const g=u*3,k=Math.min(2,Math.floor(g)),scale=p.growth[k]+(p.growth[k+1]-p.growth[k])*(g-k);
      const alpha=p.opacity*smooth(u/.2)*(1-smooth((u-.6)/.4));
      const size=(p.size+(random(seed+37)*2-1)*p.sizeVariation)*scale;
      const angle=(random(seed+41)*2-1)*p.angleVariation+t*p.spin,frame=p.atlasCount?(p.animate?Math.min(p.atlasCount-1,Math.floor(random(seed+47)*6+t*p.frames)):Math.floor(random(seed+47)*p.atlasCount)):0;
      b.offset.setXYZ(b.count,x,y,z);b.shape.setXYZW(b.count,size,angle,frame,p.aspect);b.tint.setXYZW(b.count,...p.color.map(c=>c*p.power),alpha);b.count++;counts[kind]++;
     }
    });
   }
  }
  for(const b of batches){b.geometry.instanceCount=b.count;b.offset.needsUpdate=b.shape.needsUpdate=b.tint.needsUpdate=true;}
  return counts;
 }};
}
