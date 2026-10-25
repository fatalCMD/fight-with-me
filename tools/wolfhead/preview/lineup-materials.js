// Uses the same Skyrim shader approximation as the single-effect preview.
import * as THREE from 'three';
export function materialsFor(renderer){
const loader=new THREE.TextureLoader(),textures=new Map();
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



return {glowMaterial,geometryOf};
}
