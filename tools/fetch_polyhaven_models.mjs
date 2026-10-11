// Fetch CC0 Poly Haven models (FBX + textures) into unreal/CityPainter/SourceArt/Meshes/PolyHaven/<id>/.
// Usage: node tools/fetch_polyhaven_models.mjs <id>[:<res>] ...   (default res 2k). Verifies md5 from the API.
import fs from 'fs'; import path from 'path'; import crypto from 'crypto';
const OUT = 'unreal/CityPainter/SourceArt/Meshes/PolyHaven';
const okHost = h => h === 'polyhaven.com' || h.endsWith('.polyhaven.com') || h === 'polyhaven.org' || h.endsWith('.polyhaven.org');
async function get(url) { if (!okHost(new URL(url).hostname)) throw new Error('host not allowed ' + url);
  const r = await fetch(url); if (!r.ok) throw new Error(r.status + ' ' + url); return Buffer.from(await r.arrayBuffer()); }
for (const arg of process.argv.slice(2)) {
  const [id, res = '2k'] = arg.split(':');
  const info = JSON.parse(await get(`https://api.polyhaven.com/info/${id}`));
  if (info.type !== 2) throw new Error(id + ' is not a model');
  const files = JSON.parse(await get(`https://api.polyhaven.com/files/${id}`));
  const f = files.fbx?.[res]?.fbx; if (!f) throw new Error(`${id} has no fbx ${res}`);
  const dir = path.join(OUT, id); fs.mkdirSync(dir, { recursive: true });
  const items = [[`${id}_${res}.fbx`, f], ...Object.entries(f.include || {})];
  const rec = [];
  for (const [rel, e] of items) {
    const buf = await get(e.url);
    const md5 = crypto.createHash('md5').update(buf).digest('hex');
    if (md5 !== e.md5) throw new Error('md5 mismatch ' + rel);
    const dest = path.join(dir, rel); fs.mkdirSync(path.dirname(dest), { recursive: true }); fs.writeFileSync(dest, buf);
    rec.push({ path: rel, bytes: buf.length, sha256: crypto.createHash('sha256').update(buf).digest('hex'), url: e.url });
    console.log(id, rel, buf.length);
  }
  fs.writeFileSync(path.join(dir, '_fetch.json'), JSON.stringify({ id, res, name: info.name, authors: info.authors, dimensions_api: info.dimensions, polycount_api: info.polycount, files: rec }, null, 1));
}
