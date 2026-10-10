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
writeFileSync(new URL('index.html', dir), html);
console.log('wrote index.html', html.length, 'bytes');
