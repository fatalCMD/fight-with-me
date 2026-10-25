// Read-only layout preview. Source metadata is refreshed on every request.
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const root = path.resolve(__dirname, '../..');
const mods = path.resolve(root, '../MODS/mods');
const fonts = path.join(mods, 'SKSE Menu Framework/SKSE/Plugins/fonts');
const str = value => JSON.parse(value.trim());
function metadata() {
  const source = fs.readFileSync(path.join(root, 'include/FWM/Paths.h'), 'utf8');
  const orders = fs.readFileSync(path.join(root, 'include/FWM/Orders.h'), 'utf8');
  const glyphs = Object.fromEntries([...source.matchAll(/constexpr std::uint32_t\s+(kGlyph\w+)\s*=\s*(0x[0-9a-f]+)/gi)].map(m => [m[1], Number(m[2])]));
  const glyph = value => glyphs[value.trim()] ?? Number(value.trim());
  const area = (text, begin, end) => text.slice(text.indexOf(begin), text.indexOf(end, text.indexOf(begin)));
  const ids = [...area(source, 'enum class Ability', 'inline constexpr std::size_t kAbilityCount').matchAll(/^\s*(k\w+)\s*[,\n]/gm)].map(m => m[1]);
  const abilities = [...area(source, 'abilities{ {', 'constexpr const AbilityInfo&').matchAll(/\{\s*("(?:\\.|[^"\\])*")\s*,\s*([\w]+)\s*,\s*("(?:\\.|[^"\\])*")\s*\}/g)].map((m, i) => ({ id: ids[i], name: str(m[1]), glyph: glyph(m[2]), own: str(m[3]) }));
  const nodes = [...area(source, 'paths{ {', 'struct PathInfo').matchAll(/detail::([ABTCE])\(([^)]*)\)/g)].map(m => {
    const a = m[2].match(/"(?:\\.|[^"\\])*"|[^,]+/g)?.map(x => x.trim()) ?? [];
    const kind = m[1];
    if (kind === 'E') return null;
    if (kind === 'T') return { kind: 'trait', id: a[0].split('::')[1], tier: +a[1], name: str(a[2]), glyph: glyph(a[3]), most: 3, base: 0, cost: 1, step: 1 };
    const branch = kind === 'B', capstone = kind === 'C';
    return { kind: branch ? 'branch' : 'ability', id: a[0].split('::')[1], ability: a[1].split('::')[1], tier: capstone ? 2 : +a[2], most: branch ? +a[3] : 3,
      name: str(a[branch || capstone ? 4 : 3]), glyph: glyph(a[branch || capstone ? 5 : 4]), base: 0,
      cost: capstone ? +a[2] : 1, step: capstone ? +a[3] : 1, capstone };
  });
  const info = [...area(source, 'pathInfo{ {', 'constexpr const PathInfo&').matchAll(/\{\s*("(?:\\.|[^"\\])*")\s*,\s*("(?:\\.|[^"\\])*")\s*\}/g)].map(m => ({ title: str(m[1]), lead: str(m[2]) }));
  const roles = [...area(orders, 'roles{ {', 'struct ChoiceInfo').matchAll(/\{\s*("[^"\n]*")\s*,\s*"[^"\n]*"\s*,\s*Focus::\w+\s*,\s*Position::\w+\s*,\s*(0x[\da-f]+)\s*\}/gi)].map((m, i) => ({ name: str(m[1]), glyph: +m[2], ...info[i], nodes: nodes.slice(i * 15, i * 15 + 15) }));
  if (roles.length !== 6 || nodes.length !== 90 || abilities.length < 20) throw Error('Paths metadata could not be read completely');
  return { roles, abilities, source: 'include/FWM/Paths.h', demo: true };
}
const routes = {
  '/backdrops.html': [path.join(__dirname, 'backdrops.html'), 'text/html'],
  '/': [path.join(__dirname, 'index.html'), 'text/html'],
  '/atmosphere.js': [path.join(__dirname,'atmosphere.js'),'text/javascript'],
  '/preview.js': [path.join(__dirname, 'preview.js'), 'text/javascript'],
  '/menu.ttf': [path.join(fonts, 'SkyrimMenuFont.ttf'), 'font/ttf'],
  '/theme-silver.ttf': [path.join(root,'assets/fonts/themes/Spectral-Medium.ttf'),'font/ttf'],
  '/theme-gothic.ttf': [path.join(root,'assets/fonts/themes/Lora-Medium.ttf'),'font/ttf'],
  '/theme-stone.ttf': [path.join(root,'assets/fonts/themes/BarlowSemiCondensed-Medium.ttf'),'font/ttf'],
  '/master-silver.ttf': [path.join(root,'assets/fonts/themes/Spectral-SemiBold.ttf'),'font/ttf'],
  '/master-gothic.ttf': [path.join(root,'assets/fonts/themes/Lora-SemiBold.ttf'),'font/ttf'],
  '/master-stone.ttf': [path.join(root,'assets/fonts/themes/BarlowSemiCondensed-SemiBold.ttf'),'font/ttf'],
  '/icons.ttf': [path.join(fonts, 'fa-solid-900.ttf'), 'font/ttf'],
};
for (const name of ['weak-will', 'last-bastion', 'wolf-head', 'barbed-sun'])
  routes[`/${name}.svg`] = [path.join(root, `assets/interface/FightWithMe/${name}.svg`), 'image/svg+xml'];
for (const theme of ['vanilla','oxblood','nordic'])
  for (const role of ['fighter','tank','healer','bruiser','archer','mage'])
    routes[`/backdrops/${theme}-${role}.png`] = [path.join(root,`assets/interface/FightWithMe/backdrops/${theme}-${role}.png`),'image/png'];
http.createServer((req, res) => {
  try {
    const url = new URL(req.url, 'http://localhost');
    res.setHeader('Cache-Control', 'no-store');
    if (url.pathname === '/metadata.json') {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(metadata())); return;
    }
    const entry = routes[url.pathname];
    if (!entry) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': entry[1] });
    fs.createReadStream(entry[0]).pipe(res);
  } catch (error) { res.writeHead(500); res.end(String(error)); }
}).listen(8472, '127.0.0.1', () => console.log('UI layout preview: http://127.0.0.1:8472'));
