// Skyrim SE NIF (20.2.0.7, user 12, BS stream 100) read/write for the blocks a static effect mesh
// needs: NiNode/BSFadeNode, BSTriShape, BSEffectShaderProperty, NiAlphaProperty. Every other block
// is kept as raw bytes, so any file round-trips. Also decodes NiSkinPartition's SSE vertex buffer.

class Reader {
  constructor(buf, p = 0) { this.buf = buf; this.p = p; }
  u8() { return this.buf[this.p++]; }
  u16() { const v = this.buf.readUInt16LE(this.p); this.p += 2; return v; }
  u32() { const v = this.buf.readUInt32LE(this.p); this.p += 4; return v; }
  i32() { const v = this.buf.readInt32LE(this.p); this.p += 4; return v; }
  f32() { const v = this.buf.readFloatLE(this.p); this.p += 4; return v; }
  u64() { const v = this.buf.readBigUInt64LE(this.p); this.p += 8; return v; }
  f16() { return half(this.u16()); }
  bytes(n) { const b = this.buf.subarray(this.p, this.p + n); this.p += n; return Buffer.from(b); }
  sized() { const n = this.u32(); const s = this.buf.toString('latin1', this.p, this.p + n); this.p += n; return s; }
  short() { const n = this.u8(); const s = this.buf.toString('latin1', this.p, this.p + n); this.p += n; return s; }
  refs() { const n = this.u32(); const out = []; for (let i = 0; i < n; i++) out.push(this.i32()); return out; }
}

class Writer {
  constructor() { this.parts = []; }
  push(b) { this.parts.push(b); }
  u8(v) { this.push(Buffer.from([v & 0xff])); }
  u16(v) { const b = Buffer.alloc(2); b.writeUInt16LE(v); this.push(b); }
  u32(v) { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); this.push(b); }
  i32(v) { const b = Buffer.alloc(4); b.writeInt32LE(v); this.push(b); }
  f32(v) { const b = Buffer.alloc(4); b.writeFloatLE(v); this.push(b); }
  u64(v) { const b = Buffer.alloc(8); b.writeBigUInt64LE(BigInt(v)); this.push(b); }
  f16(v) { this.u16(toHalf(v)); }
  bytes(b) { this.push(Buffer.from(b)); }
  sized(s) { this.u32(Buffer.byteLength(s, 'latin1')); this.push(Buffer.from(s, 'latin1')); }
  short(s) { this.u8(s.length); this.push(Buffer.from(s, 'latin1')); }
  refs(a) { this.u32(a.length); for (const r of a) this.i32(r); }
  done() { return Buffer.concat(this.parts); }
}

function half(h) {
  const s = h & 0x8000 ? -1 : 1, e = (h >> 10) & 0x1f, m = h & 0x3ff;
  if (e === 0) return s * Math.pow(2, -14) * (m / 1024);
  if (e === 31) return m ? NaN : s * Infinity;
  return s * Math.pow(2, e - 15) * (1 + m / 1024);
}
function toHalf(v) {
  const f = new Float32Array([v]), u = new Uint32Array(f.buffer)[0];
  const s = (u >> 16) & 0x8000; let e = ((u >> 23) & 0xff) - 127 + 15; let m = u & 0x7fffff;
  if (e <= 0) { if (e < -10) return s; m = (m | 0x800000) >> (1 - e); return s | ((m + 0x1000) >> 13); }
  if (e >= 31) return s | 0x7c00;
  const r = s | (e << 10) | (m >> 13);
  return (m & 0x1000) ? r + 1 : r;  // round half up on the dropped bits
}

const VA = { vertex: 0x1, uv: 0x2, uv2: 0x4, normal: 0x8, tangent: 0x10, color: 0x20, skinned: 0x40, eye: 0x100, full: 0x400 };
const flagsOf = desc => Number((BigInt(desc) >> 44n) & 0xfffn);

function readVertex(r, flags) {
  const v = {};
  if (flags & VA.vertex) { v.pos = [r.f32(), r.f32(), r.f32()]; if (flags & VA.tangent) v.btx = r.f32(); else v.pad = r.u32(); }
  if (flags & VA.uv) v.uv = [r.u16(), r.u16()];
  if (flags & VA.normal) { v.n = [r.u8(), r.u8(), r.u8()]; v.bty = r.u8(); }
  if (flags & VA.tangent) { v.t = [r.u8(), r.u8(), r.u8()]; v.btz = r.u8(); }
  if (flags & VA.color) v.c = [r.u8(), r.u8(), r.u8(), r.u8()];
  if (flags & VA.skinned) { v.w = [r.u16(), r.u16(), r.u16(), r.u16()]; v.bi = [r.u8(), r.u8(), r.u8(), r.u8()]; }
  if (flags & VA.eye) v.eye = r.f32();
  return v;
}
function writeVertex(w, flags, v) {
  if (flags & VA.vertex) { for (const x of v.pos) w.f32(x); if (flags & VA.tangent) w.f32(v.btx ?? 0); else w.u32(v.pad ?? 0); }
  if (flags & VA.uv) { w.u16(v.uv[0]); w.u16(v.uv[1]); }
  if (flags & VA.normal) { for (const x of v.n) w.u8(x); w.u8(v.bty ?? 0); }
  if (flags & VA.tangent) { for (const x of v.t) w.u8(x); w.u8(v.btz ?? 0); }
  if (flags & VA.color) for (const x of v.c) w.u8(x);
  if (flags & VA.skinned) { for (const x of v.w) w.u16(x); for (const x of v.bi) w.u8(x); }
  if (flags & VA.eye) w.f32(v.eye ?? 0);
}

function readNET(r, b) { b.name = r.i32(); b.extra = r.refs(); b.controller = r.i32(); }
function writeNET(w, b) { w.i32(b.name); w.refs(b.extra || []); w.i32(b.controller ?? -1); }
function readAV(r, b) {
  readNET(r, b);
  b.flags = r.u32();
  b.translate = [r.f32(), r.f32(), r.f32()];
  b.rotate = []; for (let i = 0; i < 9; i++) b.rotate.push(r.f32());
  b.scale = r.f32();
  b.collision = r.i32();
}
function writeAV(w, b) {
  writeNET(w, b);
  w.u32(b.flags); for (const x of b.translate) w.f32(x); for (const x of b.rotate) w.f32(x); w.f32(b.scale); w.i32(b.collision ?? -1);
}

const codecs = {
  NiNode: {
    read(r, b) { readAV(r, b); b.children = r.refs(); b.effects = r.refs(); },
    write(w, b) { writeAV(w, b); w.refs(b.children); w.refs(b.effects || []); }
  },
  BSTriShape: {
    read(r, b) {
      readAV(r, b);
      b.center = [r.f32(), r.f32(), r.f32()]; b.radius = r.f32();
      b.skin = r.i32(); b.shader = r.i32(); b.alpha = r.i32();
      b.desc = r.u64();
      const nt = r.u16(), nv = r.u16(); b.dataSize = r.u32();
      const flags = flagsOf(b.desc);
      b.verts = []; if (b.dataSize > 0) for (let i = 0; i < nv; i++) b.verts.push(readVertex(r, flags));
      b.nv = nv;
      b.tris = []; if (b.dataSize > 0) for (let i = 0; i < nt; i++) b.tris.push([r.u16(), r.u16(), r.u16()]);
      b.nt = nt;
      b.particleSize = r.u32();
      if (b.particleSize > 0) b.particle = r.bytes(b.particleSize);
    },
    write(w, b) {
      writeAV(w, b);
      for (const x of b.center) w.f32(x); w.f32(b.radius);
      w.i32(b.skin ?? -1); w.i32(b.shader ?? -1); w.i32(b.alpha ?? -1);
      w.u64(b.desc);
      const flags = flagsOf(b.desc);
      const nt = b.dataSize > 0 || b.tris.length ? b.tris.length : b.nt;
      const nv = b.verts.length ? b.verts.length : b.nv;
      w.u16(nt); w.u16(nv);
      // Data size counts the triangles too.
      const size = b.verts.length ? b.verts.length * Number(BigInt(b.desc) & 0xfn) * 4 + b.tris.length * 6 : b.dataSize;
      w.u32(size);
      if (size > 0) { for (const v of b.verts) writeVertex(w, flags, v); for (const t of b.tris) { w.u16(t[0]); w.u16(t[1]); w.u16(t[2]); } }
      w.u32(b.particleSize || 0); if (b.particleSize) w.bytes(b.particle);
    }
  },
  BSEffectShaderProperty: {
    read(r, b) {
      readNET(r, b);
      b.flags1 = r.u32(); b.flags2 = r.u32();
      b.uvOffset = [r.f32(), r.f32()]; b.uvScale = [r.f32(), r.f32()];
      b.source = r.sized();
      b.clamp = r.u8(); b.lighting = r.u8(); b.envMinLod = r.u8(); b.unused = r.u8();
      b.falloff = [r.f32(), r.f32(), r.f32(), r.f32()];
      b.color = [r.f32(), r.f32(), r.f32(), r.f32()];
      b.colorScale = r.f32(); b.softDepth = r.f32();
      b.greyscale = r.sized();
    },
    write(w, b) {
      writeNET(w, b);
      w.u32(b.flags1); w.u32(b.flags2);
      for (const x of b.uvOffset) w.f32(x); for (const x of b.uvScale) w.f32(x);
      w.sized(b.source);
      w.u8(b.clamp); w.u8(b.lighting); w.u8(b.envMinLod); w.u8(b.unused);
      for (const x of b.falloff) w.f32(x); for (const x of b.color) w.f32(x);
      w.f32(b.colorScale); w.f32(b.softDepth);
      w.sized(b.greyscale);
    }
  },
  NiAlphaProperty: {
    read(r, b) { readNET(r, b); b.alphaFlags = r.u16(); b.threshold = r.u8(); },
    write(w, b) { writeNET(w, b); w.u16(b.alphaFlags); w.u8(b.threshold); }
  }
};
codecs.BSFadeNode = codecs.NiNode;
// A node that turns its children to the camera: NiNode's fields, then how it turns (2 keeps them square
// to the camera, as the game's own glows are; their quads lie in the node's XY plane).
codecs.NiBillboardNode = {
  read(r, b) { codecs.NiNode.read(r, b); b.mode = r.u16(); },
  write(w, b) { codecs.NiNode.write(w, b); w.u16(b.mode ?? 2); }
};
// The lit shader every actor and object uses. The fields after the shared ones depend on the
// shader type and are kept as bytes.
codecs.BSLightingShaderProperty = {
  read(r, b, size) {
    const start = r.p;
    b.shaderType = r.u32();
    readNET(r, b);
    b.flags1 = r.u32(); b.flags2 = r.u32();
    b.uvOffset = [r.f32(), r.f32()]; b.uvScale = [r.f32(), r.f32()];
    b.textureSet = r.i32();
    b.emissive = [r.f32(), r.f32(), r.f32()]; b.emissiveMult = r.f32();
    b.clamp = r.u32(); b.alpha = r.f32(); b.refraction = r.f32(); b.glossiness = r.f32();
    b.specular = [r.f32(), r.f32(), r.f32()]; b.specularStrength = r.f32();
    b.lighting1 = r.f32(); b.lighting2 = r.f32();
    b.tail = r.bytes(size - (r.p - start));
  },
  write(w, b) {
    w.u32(b.shaderType); writeNET(w, b);
    w.u32(b.flags1); w.u32(b.flags2);
    for (const x of b.uvOffset) w.f32(x); for (const x of b.uvScale) w.f32(x);
    w.i32(b.textureSet);
    for (const x of b.emissive) w.f32(x); w.f32(b.emissiveMult);
    w.u32(b.clamp); w.f32(b.alpha); w.f32(b.refraction); w.f32(b.glossiness);
    for (const x of b.specular) w.f32(x); w.f32(b.specularStrength);
    w.f32(b.lighting1); w.f32(b.lighting2);
    w.bytes(b.tail || Buffer.alloc(0));
  }
};
codecs.BSShaderTextureSet = {
  read(r, b) { const n = r.u32(); b.textures = []; for (let i = 0; i < n; i++) b.textures.push(r.sized()); },
  write(w, b) { w.u32(b.textures.length); for (const t of b.textures) w.sized(t); }
};

function read(buf, strict = true) {
  const r = new Reader(buf);
  const headerLine = buf.toString('latin1', 0, buf.indexOf(0x0a) + 1); r.p = headerLine.length;
  const nif = { headerLine };
  nif.version = r.u32(); nif.endian = r.u8(); nif.user = r.u32();
  const count = r.u32(); nif.bsver = r.u32();
  nif.author = r.short(); nif.process = r.short(); nif.exportScript = r.short();
  const types = []; for (let i = r.u16(); i > 0; i--) types.push(r.sized());
  nif.types = types;
  const typeIndex = []; for (let i = 0; i < count; i++) typeIndex.push(r.u16());
  const sizes = []; for (let i = 0; i < count; i++) sizes.push(r.u32());
  nif.strings = []; { const n = r.u32(); r.u32(); for (let i = 0; i < n; i++) nif.strings.push(r.sized()); }
  nif.groups = []; for (let i = r.u32(); i > 0; i--) nif.groups.push(r.u32());
  nif.blocks = [];
  for (let i = 0; i < count; i++) {
    const type = types[typeIndex[i] & 0x7fff];
    const start = r.p;
    const block = { type };
    const codec = codecs[type];
    let ok = false;
    if (codec) {
      try { codec.read(r, block, sizes[i]); ok = r.p === start + sizes[i]; } catch (e) { ok = false; }
      if (!ok && strict) throw new Error(`${type} #${i}: read ${r.p - start} of ${sizes[i]} bytes`);
    }
    if (!ok) { for (const k of Object.keys(block)) if (k !== 'type') delete block[k]; r.p = start; block.raw = r.bytes(sizes[i]); }
    r.p = start + sizes[i];
    nif.blocks.push(block);
  }
  nif.roots = r.refs();
  return nif;
}

function write(nif) {
  const types = [...(nif.types || [])].filter(t => nif.blocks.some(b => b.type === t));
  for (const b of nif.blocks) if (!types.includes(b.type)) types.push(b.type);
  const bodies = nif.blocks.map(b => { if (b.raw) return b.raw; const w = new Writer(); codecs[b.type].write(w, b); return w.done(); });
  const w = new Writer();
  w.push(Buffer.from(nif.headerLine || 'Gamebryo File Format, Version 20.2.0.7\n', 'latin1'));
  w.u32(nif.version ?? 0x14020007); w.u8(nif.endian ?? 1); w.u32(nif.user ?? 12);
  w.u32(nif.blocks.length); w.u32(nif.bsver ?? 100);
  w.short(nif.author ?? ''); w.short(nif.process ?? ''); w.short(nif.exportScript ?? '');
  w.u16(types.length); for (const t of types) w.sized(t);
  for (const b of nif.blocks) w.u16(types.indexOf(b.type));
  for (const body of bodies) w.u32(body.length);
  w.u32(nif.strings.length); w.u32(nif.strings.reduce((m, s) => Math.max(m, s.length), 0)); for (const s of nif.strings) w.sized(s);
  w.u32(nif.groups.length); for (const g of nif.groups) w.u32(g);
  for (const body of bodies) w.push(body);
  w.refs(nif.roots);
  return w.done();
}

// NiSkinPartition (SSE): the global vertex buffer and each partition's bones and triangles.
function readSkinPartition(raw) {
  const r = new Reader(raw);
  const count = r.u32();
  const dataSize = r.u32(), vertexSize = r.u32(), desc = r.u64();
  const flags = flagsOf(desc);
  const verts = [];
  for (let i = 0; i < dataSize / vertexSize; i++) {
    const start = r.p; verts.push(readVertex(r, flags));
    if (r.p - start !== vertexSize) throw new Error(`vertex size ${r.p - start} != ${vertexSize}`);
  }
  const parts = [];
  for (let k = 0; k < count; k++) {
    const nv = r.u16(), nt = r.u16(), nb = r.u16(), ns = r.u16(), nw = r.u16();
    const part = { bones: [] };
    for (let i = 0; i < nb; i++) part.bones.push(r.u16());
    if (r.u8()) { part.map = []; for (let i = 0; i < nv; i++) part.map.push(r.u16()); }
    if (r.u8()) { part.weights = []; for (let i = 0; i < nv * nw; i++) part.weights.push(r.f32()); }
    const strips = []; for (let i = 0; i < ns; i++) strips.push(r.u16());
    if (r.u8()) { if (ns) for (const l of strips) r.p += 2 * l; else { part.tris = []; for (let i = 0; i < nt; i++) part.tris.push([r.u16(), r.u16(), r.u16()]); } }
    if (r.u8()) { part.boneIdx = r.bytes(nv * nw); }
    part.lod = r.u8(); part.globalVB = r.u8(); part.desc = r.u64();
    part.trisCopy = []; for (let i = 0; i < nt; i++) part.trisCopy.push([r.u16(), r.u16(), r.u16()]);
    part.nw = nw;
    parts.push(part);
  }
  if (r.p !== raw.length) throw new Error(`skin partition: read ${r.p} of ${raw.length}`);
  return { desc, flags, vertexSize, verts, parts };
}

module.exports = { read, write, readSkinPartition, Reader, Writer, half, toHalf, VA, flagsOf, readVertex };
