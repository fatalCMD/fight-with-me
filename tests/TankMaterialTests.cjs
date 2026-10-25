// Check the generated game materials, not just the animation's requested tint.
// Run after BuildBastion.js and BuildWeakWill.js with `node tests/TankMaterialTests.cjs`.
const fs = require('node:fs'), path = require('node:path'), assert = require('node:assert/strict');
const nifio = require('../tools/wolfhead/nifio.js');
const root = path.resolve(__dirname, '../assets');
const read = name => nifio.read(fs.readFileSync(path.join(root, 'meshes/FightWithMe', name)));
function sample(shader, x) {
  const dds = fs.readFileSync(path.join(root, shader.greyscale.replaceAll('\\', '/')));
  assert.equal(dds.toString('ascii', 0, 4), 'DDS ');
  assert.equal(dds.readUInt32LE(88), 32);
  const width = dds.readUInt32LE(16), height = dds.readUInt32LE(12);
  const colours = [];
  for (let y=0;y<height;y++) {
    const at=128+(y*width+Math.min(width-1,Math.floor(x*width)))*4;
    colours.push([dds[at+2],dds[at+1],dds[at]].map(c=>c/255*shader.colorScale));
  }
  return colours;
}
const castle=read('LastBastionCastle.nif');
const stones=castle.blocks.filter(b=>b.type==='BSTriShape' && castle.strings[b.name].startsWith('StoneGeometry'));
assert.ok(stones.length>0);
for(const shape of stones) {
  const shader=castle.blocks[shape.shader],alpha=castle.blocks[shape.alpha];
  assert.ok(shader.flags1 & 0x10, 'castle retains textured palette shading');
  assert.equal(alpha.alphaFlags,0x10ed,'masonry remains translucent');
  // Palette colour replaces runtime RGB in Skyrim. Every texel row must itself be red.
  for(const value of [0,.05,.2,.5,.8,1]) for(const [r,g,b] of sample(shader,value)) {
    assert.ok(r>g*8 && r>b*8,'rank III masonry palette must be crimson without a runtime tint');
    assert.ok(r<=1,'masonry emission stays below white-hot clipping');
  }
}
for(const grand of ['', 'Grand']) for(const copy of ['', '2', '3', '4']) {
  const nif=read(`WeakWillHelmet${grand}${copy}.nif`);
  const body=nif.blocks.find(b=>b.type==='BSTriShape' && ['EbonyHelmet','SteelHelmet'].includes(nif.strings[b.name]));
  const rim=nif.blocks.find(b=>b.type==='BSTriShape' && nif.strings[b.name]==='HelmetEdge');
  assert.ok(body && rim,'both textured helmet and rim are present');
  assert.ok(nif.blocks[body.shader].flags1 & 0x10,'helmet body retains its steel/ebony palette');
  const shader=nif.blocks[rim.shader],alpha=nif.blocks[rim.alpha];
  assert.equal(shader.flags1 & 0x30,0,'rim must not replace animated tint or alpha with a palette');
  assert.equal(shader.greyscale,'','rim has no stale palette binding');
  assert.equal(shader.color[3],0,'rim starts transparent while its effect is loading');
  assert.equal((alpha.alphaFlags>>1)&15,6,'rim uses source alpha blending');
  assert.equal((alpha.alphaFlags>>5)&15,0,'rim adds its light using source alpha, not an opaque replacement');
}
console.log('Tank material regressions passed: crimson masonry and all eight fading helmet variants.');
