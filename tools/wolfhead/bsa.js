// SE BSA (v105) reader: list and read files, LZ4-compressed or not.
const fs = require('fs');
const { frame } = require('./lz4.js');
function open(archive) {
  const buf = fs.readFileSync(archive);
  const offset = buf.readUInt32LE(8), flags = buf.readUInt32LE(12);
  const folders = buf.readUInt32LE(16), files = buf.readUInt32LE(20);
  let pos = offset;
  const counts = [];
  for (let i = 0; i < folders; i++) { counts.push(buf.readUInt32LE(pos + 8)); pos += 24; }
  const recs = [];
  for (let i = 0; i < folders; i++) {
    const len = buf[pos]; const dir = buf.toString('latin1', pos + 1, pos + len).replace(/\0$/, ''); pos += 1 + len;
    for (let j = 0; j < counts[i]; j++) { recs.push({ dir, size: buf.readUInt32LE(pos + 8), at: buf.readUInt32LE(pos + 12) }); pos += 16; }
  }
  let k = 0, n = '';
  for (let i = 0; k < files; i++) { const ch = buf[pos + i]; if (ch === 0) { recs[k++].name = n; n = ''; } else n += String.fromCharCode(ch); }
  for (const r of recs) r.path = r.dir + '\\' + r.name;
  const read = r => {
    let at = r.at, size = r.size & 0x3fffffff;
    if (flags & 0x100) { const l = buf[at]; at += 1 + l; size -= 1 + l; }
    let data = buf.subarray(at, at + size);
    if (((r.size & 0x40000000) !== 0) !== ((flags & 4) !== 0)) data = frame(data.subarray(4));
    return data;
  };
  return { files: recs, read };
}
module.exports = { open };
