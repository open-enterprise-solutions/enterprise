// preview.js - every picture of the engine on one page: each `s_<name>_svg` a source file holds, drawn on the grounds
// the platform puts it on - the light theme's (a window, a form, the chrome) and a dark one - at the size it is shown
// (16) and at twice that. A picture's source IS that string: change the drawing there, look at it here.
//
//   node tools/pictures/preview.js [page.html]     writes the page (by default into the temporary folder) and says where
//
// The browser draws an SVG more fully than the engine's NanoSVG: no <text> (letters are paths), no CSS, no filters.
// What is not drawn in the engine is not drawn here either only as long as a drawing keeps to that.
//
// At the end, the drawings that stand in more than one place under different names - one act, one picture, so a
// change made in one of them has to be made in the others.

const fs = require('fs'), path = require('path'), os = require('os');

const ENGINE = path.resolve(__dirname, '..', '..', 'src', 'engine');

// Every `s_<name>_svg = [wxT(|ibT(] "…" ["…"…]` under src/engine: { name, file, svg }.
function scan(root = ENGINE) {
  const pictures = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(full);
      else if (/\.(h|hpp|cpp)$/.test(entry.name)) read(full);
    }
  };
  const read = (file) => {
    const text = fs.readFileSync(file, 'utf8');
    const declaration = /\b(s_\w+_svg)\s*(?:\[\])?\s*=\s*(?:(?:wxT|ibT)\s*\(\s*)?/g;
    for (let m; (m = declaration.exec(text));) {
      let at = declaration.lastIndex, svg = '';
      for (;;) {
        const literal = /^\s*"((?:[^"\\]|\\.)*)"/.exec(text.slice(at));
        if (!literal) break;
        svg += literal[1].replace(/\\(["\\])/g, '$1');
        at += literal[0].length;
      }
      if (svg.startsWith('<svg'))
        pictures.push({ name: m[1], file: path.relative(root, file).replace(/\\/g, '/'), svg });
    }
  };
  walk(root);
  return pictures.sort((a, b) => a.file.localeCompare(b.file) || a.name.localeCompare(b.name));
}

const GROUNDS = [
  { name: 'window', colour: '#ffffff', ink: '#3f5c77' },
  { name: 'form', colour: '#eef2f6', ink: '#3f5c77' },
  { name: 'chrome', colour: '#dde4e8', ink: '#3f5c77' },
  { name: 'dark', colour: '#1f2428', ink: '#a8bac8' },
  { name: 'dark panel', colour: '#2b3238', ink: '#a8bac8' },
];

function page(pictures) {
  const escape = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  const image = (svg, side) =>
    `<img width="${side}" height="${side}" src="data:image/svg+xml;utf8,${encodeURIComponent(svg)}">`;
  const cell = (p) => `<div class="cell" title="${escape(p.file)}">${image(p.svg, 16)}${image(p.svg, 32)}` +
    `<span>${escape(p.name.replace(/^s_|_svg$/g, ''))}</span></div>`;

  const byFolder = new Map();
  for (const p of pictures) {
    const folder = path.posix.dirname(p.file);
    if (!byFolder.has(folder)) byFolder.set(folder, []);
    byFolder.get(folder).push(p);
  }
  const sheet = (ground) => `<section style="background:${ground.colour};color:${ground.ink}">` +
    `<h2>${ground.name} ${ground.colour}</h2>` +
    [...byFolder].map(([folder, list]) => `<h3>${escape(folder)}</h3><div class="grid">${list.map(cell).join('')}</div>`).join('') +
    `</section>`;

  const same = new Map();
  for (const p of pictures) {
    if (!same.has(p.svg)) same.set(p.svg, []);
    same.get(p.svg).push(p);
  }
  const repeated = [...same.values()].filter((list) => list.length > 1);
  const twins = repeated.length === 0 ? '<p>none</p>' : '<ul>' + repeated.map((list) =>
    `<li>${image(list[0].svg, 16)} ${list.map((p) => `${escape(p.name)} <i>${escape(p.file)}</i>`).join(' · ')}</li>`).join('') + '</ul>';

  return `<!doctype html><html><head><meta charset="utf-8"><title>Engine pictures</title><style>
body{margin:0;font:12px Segoe UI,sans-serif}
section{padding:12px 16px}
h2{margin:0 0 8px;font-size:14px}
h3{margin:12px 0 4px;font-size:12px;font-weight:600;opacity:.8}
.grid{display:flex;flex-wrap:wrap;gap:4px}
.cell{display:flex;align-items:center;gap:6px;width:220px;padding:2px 4px}
.cell span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
img{flex:none}
.twins{background:#fff;color:#000}
.twins li{margin:4px 0}
.twins i{color:#5a7b95;font-style:normal}
</style></head><body>
<section class="twins"><h2>${pictures.length} pictures in ${byFolder.size} folders</h2></section>
${GROUNDS.map(sheet).join('\n')}
<section class="twins"><h2>The same drawing in more than one place</h2>${twins}</section>
</body></html>`;
}

if (require.main === module) {
  const out = path.resolve(process.argv[2] || path.join(os.tmpdir(), 'engine-pictures.html'));
  const pictures = scan();
  fs.writeFileSync(out, page(pictures));
  console.log(`${pictures.length} pictures: ${out}`);
}

module.exports = { scan };
