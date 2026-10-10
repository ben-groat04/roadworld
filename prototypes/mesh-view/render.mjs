// Renders export_mesh output with three.js in a headless-ish Edge/Chrome (same launch logic as
// prototypes/road-draw/tools/scene.mjs).
//   node render.mjs --in <export dir> --out <png dir> [--views views.json] [--only name,name] [--tex <PolyHaven dir>]
// Views are in SIM metres (x east, y north, z up); see views.json.
// Layout cameras: when <export dir>/export.json has "cameras" (export_mesh --layout) and --views is not given, each
// camera is rendered at its matched photo's pixel size (photo file name from the camera's "photo", looked up in
// --photos, default docs/reference/gsv) with its horizontal FOV. With --compare, a side-by-side composite
// (reference photo | divider | ours, labelled) is also written to docs/reference/gsv/compare/<camera>.png: that folder is the
// gitignored private one (the composite contains the copyrighted photo), and the script refuses to write there unless
// `git check-ignore` confirms it. Our render alone goes to --out (use prototypes/road-draw/progress/ref/, gitignored).
//   node render.mjs --in <export dir> --out ../road-draw/progress/ref --compare [--only camera,camera]
import http from 'node:http';
import { readFileSync, writeFileSync, existsSync, statSync, mkdirSync, createReadStream } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { resolve, join, extname, dirname, sep } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
let chromium;
try { ({ chromium } = require('playwright-core')); }
catch { ({ chromium } = require(resolve(here, '..', 'road-draw', 'node_modules', 'playwright-core'))); }

const args = process.argv.slice(2);
const opt = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const inDir = opt('--in'), outDir = opt('--out');
if (!inDir || !outDir) { console.error('usage: node render.mjs --in <export dir> --out <png dir> [--views file] [--only a,b] [--tex dir]'); process.exit(2); }
const exportDir = resolve(inDir);
const texDir = resolve(opt('--tex', join(here, '..', '..', 'unreal', 'CityPainter', 'SourceArt', 'Textures', 'PolyHaven')));
const repoRoot = resolve(here, '..', '..');
const photoDir = resolve(opt('--photos', join(repoRoot, 'docs', 'reference', 'gsv')));
const compare = args.includes('--compare');
const exportJson = JSON.parse(readFileSync(join(exportDir, 'export.json'), 'utf8'));
const pngSize = f => { const b = readFileSync(f); return [b.readUInt32BE(16), b.readUInt32BE(20)]; };
let views;
if (opt('--views') || !(exportJson.cameras || []).length) {
  views = JSON.parse(readFileSync(resolve(opt('--views', join(here, 'views.json'))), 'utf8'));
} else {
  views = exportJson.cameras.map(c => {
    const v = { name: c.name, pos: c.pos, target: c.target, hfov: c.fov_deg, near: 0.2, shadow: 160, exposure: 0.9, photo: c.photo };
    const pf = c.photo ? join(photoDir, c.photo) : null;
    if (pf && existsSync(pf)) { [v.width, v.height] = pngSize(pf); }
    else if (c.aspect > 0) { v.height = 850; v.width = Math.round(850 * c.aspect); }
    return v;
  });
}
let compareDir = null;
if (compare) {
  compareDir = join(photoDir, 'compare');
  mkdirSync(compareDir, { recursive: true });
  try { execFileSync('git', ['check-ignore', '-q', join(compareDir, 'x.png')], { cwd: repoRoot }); }
  catch { console.error('refusing to write composites: ' + compareDir + ' is not gitignored'); process.exit(3); }
}
const only = opt('--only') ? opt('--only').split(',') : null;
mkdirSync(resolve(outDir), { recursive: true });

const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.obj': 'text/plain', '.jpg': 'image/jpeg', '.png': 'image/png' };
const roots = [['/export/', exportDir], ['/tex/', texDir], ['/photo/', photoDir]];
const server = http.createServer((req, res) => {
  let u = decodeURIComponent((req.url || '/').split('?')[0]), file = null;
  if (u === '/viewer.html') file = join(here, 'viewer.html');
  else if (u === '/mesh-view.js') file = join(here, 'mesh-view.js');
  else for (const [prefix, root] of roots) if (u.startsWith(prefix)) {
    const f = resolve(root, '.' + u.slice(prefix.length - 1));
    if (f === root || f.startsWith(root + sep)) file = f;
  }
  if (!file || !existsSync(file) || !statSync(file).isFile()) { res.writeHead(404); res.end('not found'); return; }
  res.writeHead(200, { 'content-type': MIME[extname(file)] || 'application/octet-stream' });
  createReadStream(file).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const port = server.address().port;

const launchArgs = process.platform === 'linux' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : process.platform === 'win32' ? ['--use-angle=d3d11', '--force_high_performance_gpu'] : [];
let browser = null; const launchErrs = [];
for (const o of [{ channel: 'msedge' }, { channel: 'chrome' }, {}]) {
  try { browser = await chromium.launch({ ...o, args: launchArgs }); break; } catch (e) { launchErrs.push((o.channel || 'chromium') + ': ' + String(e.message).split('\n')[0]); }
}
if (!browser) { console.error('no usable browser:\n' + launchErrs.join('\n')); server.close(); process.exit(4); }
const page = await browser.newPage({ viewport: { width: 1600, height: 900 } });
const errors = [];
page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') errors.push(m.type() + ': ' + m.text()); });
page.on('pageerror', e => errors.push('pageerror: ' + e.message));
let code = 0;
try {
  await page.goto(`http://127.0.0.1:${port}/viewer.html`);
  await page.waitForFunction('window.__ready === true', null, { timeout: 180000 });
  console.log((await page.evaluate('window.__info.report')).join('\n'));
  for (const v of views) {
    if (only && !only.includes(v.name)) continue;
    const t = Date.now();
    await page.evaluate(vv => window.setView(vv), v);
    await page.waitForTimeout(300);
    const file = join(resolve(outDir), v.name + '.png');
    if (v.width) writeFileSync(file, Buffer.from((await page.evaluate('window.snap()')).split(',')[1], 'base64'));
    else await page.screenshot({ path: file });
    console.log(`${file} (${Date.now() - t} ms)`);
    if (compare && v.photo && existsSync(join(photoDir, v.photo))) {
      const url = await page.evaluate(p => window.compose(p), '/photo/' + encodeURIComponent(v.photo));
      const cf = join(compareDir, v.name + '.png');
      writeFileSync(cf, Buffer.from(url.split(',')[1], 'base64'));
      console.log(`${cf} (composite)`);
    }
  }
} catch (e) { console.error(String(e)); code = 1; }
if (errors.length) console.error(errors.slice(0, 15).join('\n'));
await browser.close(); server.close();
process.exit(code);
