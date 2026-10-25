// LZ4 frame decoder (enough for SE BSA files: independent or linked blocks, no dictionary).
function block(src, out) {
  let i = 0;
  while (i < src.length) {
    const tok = src[i++];
    let lit = tok >> 4;
    if (lit === 15) { let b; do { b = src[i++]; lit += b; } while (b === 255); }
    for (let k = 0; k < lit; k++) out.push(src[i++]);
    if (i >= src.length) break;
    const off = src[i] | (src[i + 1] << 8); i += 2;
    let len = tok & 15;
    if (len === 15) { let b; do { b = src[i++]; len += b; } while (b === 255); }
    len += 4;
    const from = out.length - off;
    for (let k = 0; k < len; k++) out.push(out[from + k]);
  }
}
function frame(buf) {
  let p = 0;
  if (buf.readUInt32LE(0) !== 0x184D2204) throw new Error('not lz4 frame');
  p = 4;
  const flg = buf[p], bd = buf[p + 1]; p += 2;
  if (flg & 8) p += 8;  // content size
  if (flg & 1) p += 4;  // dict id
  p += 1;               // header checksum
  const out = [];
  for (;;) {
    const size = buf.readUInt32LE(p); p += 4;
    if (size === 0) break;
    const raw = size & 0x80000000, n = size & 0x7fffffff;
    const data = buf.subarray(p, p + n); p += n;
    if (raw) for (const b of data) out.push(b); else block(data, out);
    if (flg & 0x10) p += 4;  // block checksum
  }
  return Buffer.from(out);
}
module.exports = { frame };
