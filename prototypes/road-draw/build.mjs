// Builds index.html from src/: node build.mjs (plain Node, no dependencies).
import { readFileSync, writeFileSync } from 'node:fs';
const dir = new URL('./', import.meta.url);
const rd = f => readFileSync(new URL(`src/${f}`, dir), 'utf8');
// Fixed concatenation order; this is the load order inside the single module scope.
const APP = ['config.js', 'scene.js', 'terrain.js', 'earthworks.js', 'roadmesh.js', 'inspect.js', 'input.js', 'ui.js'];
const body = s => s.replace(/\n+$/, '') + '\n';
let html = rd('shell.html');
html = html.replace('<!--GEOM-->', () => body(rd('geom.js')).trimEnd());
html = html.replace('<!--APP-->', () => APP.map(f => body(rd(f))).join('\n').trimEnd());
html = html.replace('<!doctype html>', () => '<!doctype html>\n<!-- GENERATED, edit src/ and run node build.mjs -->');
// --out <file>: write there instead of the tracked index.html (the hand-in gate builds into a temp dir and never touches the worktree it inspects).
const oi = process.argv.indexOf('--out');
const target = oi >= 0 ? process.argv[oi + 1] : null;
writeFileSync(target ? target : new URL('index.html', dir), html);
console.log('wrote', target || 'index.html', html.length, 'bytes');
