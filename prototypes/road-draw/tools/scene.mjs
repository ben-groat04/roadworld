// Canonical test scene + fixed camera views for road-draw. Everyone checks the same hard cases.
//
//   node tools/scene.mjs <outDir> [viewName ...]      (default: all views)
//   INFO=1 node tools/scene.mjs <outDir> overview     (also prints per-feature structure stats)
//   node tools/scene.mjs --check                      (no screenshots: numeric invariant checks, one PASS/FAIL line each, exit 1 on any FAIL)
//
// Loads ../index.html (run `node build.mjs` first), draws the scene through window.__proto.drawRoad,
// closes help, hides the profile panel, then writes <outDir>/<view>.png at 1400x800.
// Prints console errors and exits 1 if there are any.
import { chromium } from '/tmp/claude-0/-home-user-roadworld/60bece6d-438c-5d30-8fba-60a1ef284c2a/scratchpad/node_modules/playwright/index.mjs';
import { mkdirSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const PAGE = 'file://' + resolve(HERE, '..', 'index.html');

// ===================== TWEAKABLE CONSTANTS (world metres; x east, y north; terrain seed 1337) =====================
// Road type ids: 1 residential, 2 B road, 3 A road dual, 4 motorway.
// Terrain notes: water level 14 m; valley bottom ~(1300,1400) (ground ~7 m); escarpment rises 50 -> 90 m around x=300, y=1300..1500.

// 1. cross_slope: two residential roads crossing at grade on a ~8% side slope (gradient 0.083 at this spot).
//    Road A runs along the contour (side slope), road B runs straight up the fall line (8% grade). Directions are derived from the terrain gradient.
const CROSS = { c: [1080, 200], half: 80 };
// 2. tee_slope: residential T-junction on a ~9% slope. Main street along the contour, stem comes up the fall line and ends on the main street.
const TEE = { c: [1240, 400], half: 80, stem: 80 };
// 3. hillside: B road along a ~38% hillside (uphill on its +y side = cut, downhill side = fill / retaining wall).
const HILL = { c: [425, 200], half: 110 };
// 4. viaduct: B road across the deepest part of the valley (ground ~7 m, water 14 m). Deck heights are explicit (z) so the grade cap
//    does not stop the road, giving a bridge ~230 m long with several piers.  [x, y, z]
const VIADUCT = [[1100, 1400, 40.5], [1325, 1400, 40], [1550, 1400, 42]];
// 5. tunnel: residential road heading north-west->south-east... (heading 300 deg: +x/-y) straight through a 360 m ridge (spur of the high ground,
//    up to ~32 m above the road) so there are TWO portals; z is a straight ramp 69.3 -> 46.7 (6.3% grade). Chosen by scanning the terrain for a
//    clean through-ridge; the first tunnel sample (the entry portal) is found automatically and the views are placed relative to it.  [x, y, z]
const TUNNEL = [[550, 650, 69.3], [640, 494, 58], [730, 338, 46.7]];
const TUNNEL_FRONT_DIST = 40;   // m in front of the portal, along the road
// 6. dual_curve: A-road dual carriageway, ONE stroke, big arc (minR 720). Points on a circle: centre, radius, start/end angle (deg), count.
const DUAL = { cc: [1000, -300], R: 1000, a0: 25, a1: 60, n: 9 };
// 7. motorway on embankment: straight 360 m run west along y=1800 over undulating ground, deck held level at 79.3 m = the highest
//    ground on the line, so the road is on fill (mean ~7 m, max ~10 m, below the 12 m bridge cap) with no cuts. [x, y, z]
const MOTORWAY = [[900, 1800, 79.3], [720, 1800, 79.3], [540, 1800, 79.3]];

// Camera views. target = [x, y] (terrain target), dist = camera distance from target (m), elev = degrees looking down,
// look = compass-style heading the camera LOOKS toward, degrees (0 = +x east, 90 = +y north), or 'dyn:<name>' computed at runtime.
// Registered below after the scene is drawn (some depend on measured geometry).
const OBLIQUE = { dist: 55, elev: 22 };
const VIEWS = {
  cross_slope: () => ({ target: CROSS.c, dist: 60, elev: 24, look: info.cross.upHeading }),
  tee_slope: () => ({ target: TEE.c, dist: 60, elev: 24, look: info.tee.upHeading }),
  hillside: () => ({ target: HILL.c, dist: 75, elev: 20, look: info.hill.upHeading }),   // from the fill side, looking uphill at the cut
  viaduct: () => ({ target: [1325, 1400], dist: 130, elev: 16, look: 55 }),
  tunnel_front: () => ({ target: info.portal.xy, dist: TUNNEL_FRONT_DIST, elev: 8, look: info.portal.heading }),
  tunnel_side: () => ({ target: info.portal.xy, dist: TUNNEL_FRONT_DIST, elev: 14, look: info.portal.heading + 45 }),
  dual_curve: () => ({ target: info.dualMid, dist: 90, elev: 22, look: info.dualHeading + 35 }),
  motorway: () => ({ target: [720, 1800], dist: 100, elev: 18, look: 60 }),
  overview: () => ({ target: [850, 850], dist: 2700, elev: 50, look: 100 }),
};
// ======================================================================================================================

const CHECK_MODE = process.argv.includes('--check');
const argv = process.argv.slice(2).filter(a => a !== '--check');
const outDir = resolve(argv[0] || '.');
const wanted = argv.slice(1);
const names = wanted.length ? wanted : Object.keys(VIEWS);
for (const n of names) if (!VIEWS[n]) { console.error('unknown view ' + n + '; have: ' + Object.keys(VIEWS).join(' ')); process.exit(2); }
mkdirSync(outDir, { recursive: true });

const t0 = Date.now();
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] });
const page = await browser.newPage({ viewport: { width: 1400, height: 800 } });
const errors = [];
page.on('console', m => { if (m.type() === 'error') errors.push('console.error: ' + m.text()); });
page.on('pageerror', e => errors.push('pageerror: ' + e.message));
await page.goto(PAGE);
await page.waitForFunction(() => window.__proto);

// Close help, hide profile panel (the key handler toggles state; profile is on by default).
await page.evaluate(() => { const P = window.__proto; if (P.state.help) document.getElementById('help').click(); });
if (await page.evaluate(() => window.__proto.state.profile)) await page.keyboard.press('p');
await page.evaluate(() => { for (const id of ['hud', 'tune']) { const e = document.getElementById(id); if (e) e.style.display = 'none'; } });

// Draw the scene in the page. Returns derived geometry used by the views.
const info = await page.evaluate(({ CROSS, TEE, HILL, VIADUCT, TUNNEL, DUAL, MOTORWAY }) => {
  const P = window.__proto, h = P.hAt;
  const deg = r => r * 180 / Math.PI;
  // Unit vectors at (x,y): u = uphill (fall line), t = contour tangent (u rotated +90 deg).
  const frame = (x, y) => {
    const gx = (h(x + 10, y) - h(x - 10, y)) / 20, gy = (h(x, y + 10) - h(x, y - 10)) / 20, s = Math.hypot(gx, gy) || 1;
    const u = [gx / s, gy / s]; return { u, t: [-u[1], u[0]], slope: s, upHeading: deg(Math.atan2(u[1], u[0])) };
  };
  const add = (c, v, k) => [c[0] + v[0] * k, c[1] + v[1] * k];
  const out = {}, stats = {};
  const roadsBefore = () => P.roads().length;
  const draw = (name, type, pts) => {
    const n0 = roadsBefore(); P.drawRoad(type, pts);
    const made = P.roads().slice(n0);
    stats[name] = { drawn: made.length, ...made.reduce((a, r) => {
      const S = r.samples; a.samples += S.length; a.bridge += S.filter(s => s.bridge).length; a.tunnel += S.filter(s => s.tunnel).length;
      a.wall += S.filter(s => s.wL || s.wR).length; a.fill += S.filter(s => s.z - s.t > 0.5).length; a.cut += S.filter(s => s.t - s.z > 0.5).length; return a;
    }, { samples: 0, bridge: 0, tunnel: 0, wall: 0, fill: 0, cut: 0 }) };
    return made;
  };
  // 1
  { const f = frame(...CROSS.c); out.cross = { upHeading: f.upHeading, slope: f.slope };
    draw('cross_slope A (contour)', 1, [add(CROSS.c, f.t, -CROSS.half), CROSS.c, add(CROSS.c, f.t, CROSS.half)]);
    draw('cross_slope B (fall line)', 1, [add(CROSS.c, f.u, -CROSS.half), CROSS.c, add(CROSS.c, f.u, CROSS.half)]); }
  // 2
  { const f = frame(...TEE.c); out.tee = { upHeading: f.upHeading, slope: f.slope };
    draw('tee main', 1, [add(TEE.c, f.t, -TEE.half), TEE.c, add(TEE.c, f.t, TEE.half)]);
    draw('tee stem', 1, [add(TEE.c, f.u, -TEE.stem), add(TEE.c, f.u, -TEE.stem / 2), TEE.c]); }
  // 3
  { const f = frame(...HILL.c); out.hill = { upHeading: f.upHeading, slope: f.slope };
    draw('hillside', 2, [add(HILL.c, f.t, -HILL.half), HILL.c, add(HILL.c, f.t, HILL.half)]); }
  // 4
  draw('viaduct', 2, VIADUCT);
  // 5
  { const [r] = draw('tunnel', 1, TUNNEL); const S = r ? r.samples : [], i = S.findIndex(s => s.tunnel);
    const k = i > 0 ? i : 0, s = S[k] || { x: TUNNEL[1][0], y: TUNNEL[1][1], h: Math.PI / 2 };
    out.portal = { xy: [s.x, s.y], heading: deg(s.h), found: i > 0 }; }
  // 6
  { const pts = [], a0 = DUAL.a0 * Math.PI / 180, a1 = DUAL.a1 * Math.PI / 180;
    for (let k = 0; k < DUAL.n; k++) { const a = a0 + (a1 - a0) * k / (DUAL.n - 1); pts.push([DUAL.cc[0] + DUAL.R * Math.cos(a), DUAL.cc[1] + DUAL.R * Math.sin(a)]); }
    draw('dual_curve (centre+2 carriageways)', 3, pts);
    const am = (a0 + a1) / 2; out.dualMid = [DUAL.cc[0] + DUAL.R * Math.cos(am), DUAL.cc[1] + DUAL.R * Math.sin(am)];
    out.dualHeading = deg(am + Math.PI / 2); }
  // 7
  draw('motorway', 4, MOTORWAY);
  P.flushGrade();
  out.stats = stats; out.roadCount = P.roads().length;
  return out;
}, { CROSS, TEE, HILL, VIADUCT, TUNNEL, DUAL, MOTORWAY });

if (CHECK_MODE) {
  const results = await page.evaluate(CHECKS_IN_PAGE);
  await page.waitForTimeout(300);
  const consoleErrs = [...new Set(errors)];
  results.push({ name: 'no_console_errors', pass: consoleErrs.length === 0, msg: consoleErrs.length ? consoleErrs.join(' | ') : '0 errors' });
  // perf: CPU-side frame cost over 120 frames with the camera orbiting its target (GPU timing is meaningless under swiftshader)
  { const pf = await page.evaluate(() => {
      const P = window.__proto, pf = window.__perf, c = P.camera, tg = P.controls.target; let now = performance.now();
      const step = () => { const rx = c.position.x - tg.x, rz = c.position.z - tg.z, a = 0.02, co = Math.cos(a), si = Math.sin(a);
        c.position.x = tg.x + rx * co - rz * si; c.position.z = tg.z + rx * si + rz * co; P.controls.update(); now += 16.7; pf.step(now); };
      for (let i = 0; i < 10; i++) step();
      let calls = 0, tris = 0, maxCalls = 0; const t0 = performance.now();
      for (let i = 0; i < 120; i++) { step(); const r = pf.info.render; calls = r.calls; tris = r.triangles; maxCalls = Math.max(maxCalls, calls); }
      return { ms: (performance.now() - t0) / 120, calls: maxCalls, tris };
    });
    results.push({ name: 'perf', pass: pf.calls <= 300, msg: `draw calls ${pf.calls} (max 300), triangles ${pf.tris}, JS ${pf.ms.toFixed(2)} ms/frame over 120 orbit frames` }); }
  let bad = 0;
  for (const r of results) { if (!r.pass) bad++; console.log(`${r.pass ? 'PASS' : 'FAIL'} ${r.name.padEnd(24)} ${r.msg}`); }
  await browser.close();
  console.log(`${results.length - bad}/${results.length} passed in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
  process.exit(bad ? 1 : 0);
}
if (process.env.INFO) console.log(JSON.stringify(info, null, 1));
if (!info.portal.found) errors.push('scene: no tunnel portal found (tunnel road has no tunnel samples)');

for (const name of names) {
  const v = VIEWS[name]();
  await page.evaluate(({ v }) => {
    const P = window.__proto, az = Math.atan2(-Math.cos(v.look * Math.PI / 180), Math.sin(v.look * Math.PI / 180));
    P.setView(v.target[0], v.target[1], v.dist, az, (90 - v.elev) * Math.PI / 180);
    P.renderNow();
  }, { v });
  await page.waitForTimeout(250);   // let the main loop apply updateCamera (terrain-following) before the final render
  await page.evaluate(() => window.__proto.renderNow());
  await page.screenshot({ path: resolve(outDir, name + '.png') });
}

await browser.close();
const secs = ((Date.now() - t0) / 1000).toFixed(1);
console.log(`wrote ${names.length} view(s) to ${outDir} in ${secs}s: ${names.join(' ')}`);
if (errors.length) { console.error(errors.length + ' error(s):\n' + [...new Set(errors)].join('\n')); process.exit(1); }

// ============================== numeric invariant checks (run inside the page; must be self-contained) ==============================
// Meshes carry userData.kind (roadmesh.js / terrain.js): surface | wall | pier | deck | abutment | parapet | portal | lining | terrain | water | skirt.
// Earthwork meshes (earthworks.js) are untagged: any untagged mesh inside a road group is 'earthwork'. 'structure' = wall/pier/deck/abutment/parapet/portal.
// Terrain and water are not raycast (513x513 mesh): they are analytic (terrain = P.hAt, which matches the linear mesh; masked per the corridor mask).
function CHECKS_IN_PAGE() {
  const P = window.__proto, THREE = P.THREE, T = P.T, hAt = P.hAt, RT = P.ROAD_TYPES, WORLD = 2048, DECK_T = 1.5;
  P.renderNow();   // builds road meshes, cross-sections and the corridor mask
  const results = [];
  const fmt = (x, y) => `(${x.toFixed(1)},${y.toFixed(1)})`;
  const done = (name, worst, count, total, okMsg, unit = 'm') => results.push({ name, pass: count === 0, msg: count === 0 ? okMsg : `${count}/${total} bad; worst ${worst.what} at ${fmt(worst.x, worst.y)} road#${worst.road}${unit ? ' ' + worst.v.toFixed(3) + unit : ''}` });
  const roads = P.roads();
  const roadGroup = P.scene.children.find(g => g.isGroup && (() => { let f = false; g.traverse(o => { if (o.userData && o.userData.road) f = true; }); return f; })());
  if (!roadGroup) return [{ name: 'setup', pass: false, msg: 'no road group in scene' }];

  // ---- corridor mask (replicates the terrain shader's eroded discard test with nearest taps) ----
  const mc = P.maskCanvas, md = mc.getContext('2d').getImageData(0, 0, WORLD, WORLD).data;
  const mv = (x, y) => { const i = Math.min(WORLD - 1, Math.max(0, Math.floor(x))), j = Math.min(WORLD - 1, Math.max(0, Math.floor(y))); return md[(j * WORLD + i) * 4] / 255; };
  const masked = (x, y) => Math.min(mv(x, y), mv(x + 1, y + 1), mv(x - 1, y + 1), mv(x + 1, y - 1), mv(x - 1, y - 1)) > 0.55;

  // ---- spatial grid over every road mesh triangle (three coords: X = x, Z = -y) ----
  const CS = 4, grid = new Map(), tris = [];   // tris: [ax,ay,az,bx,by,bz,cx,cy,cz,kind,roadId]
  const STRUCT = new Set(['wall', 'pier', 'deck', 'abutment', 'parapet', 'portal']);
  const kindOf = o => { const k = o.userData.kind; return !k ? 'earthwork' : STRUCT.has(k) ? 'structure' : 'surface'; };
  const meshes = []; roadGroup.traverse(o => { if (o.isMesh) meshes.push(o); });
  for (const m of meshes) {
    const k = kindOf(m), rid = m.userData.road ? m.userData.road.id : -1, pos = m.geometry.attributes.position.array, idx = m.geometry.index.array;
    for (let t = 0; t < idx.length; t += 3) {
      const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
      const rec = [pos[a], pos[a + 1], pos[a + 2], pos[b], pos[b + 1], pos[b + 2], pos[c], pos[c + 1], pos[c + 2], k, rid];
      const id = tris.push(rec) - 1;
      const x0 = Math.min(rec[0], rec[3], rec[6]), x1 = Math.max(rec[0], rec[3], rec[6]), z0 = Math.min(rec[2], rec[5], rec[8]), z1 = Math.max(rec[2], rec[5], rec[8]);
      for (let i = Math.floor(x0 / CS); i <= Math.floor(x1 / CS); i++) for (let j = Math.floor(z0 / CS); j <= Math.floor(z1 / CS); j++) { const key = i * 100003 + j; let l = grid.get(key); if (!l) grid.set(key, l = []); l.push(id); }
    }
  }
  // Up-facing triangle hits above a point: [{y, kind, rid}]
  const upHits = (X, Z, eps = 2e-3) => {
    const l = grid.get(Math.floor(X / CS) * 100003 + Math.floor(Z / CS)), out = [];
    if (!l) return out;
    for (const id of l) {
      const q = tris[id], ux = q[3] - q[0], uz = q[5] - q[2], vx = q[6] - q[0], vz = q[8] - q[2], den = uz * vx - ux * vz;   // = normal.y * 1 (cross(u,v).y)
      if (den <= 1e-9) continue;   // down-facing or degenerate
      const px = X - q[0], pz = Z - q[2], b = (pz * vx - px * vz) / den, c = (uz * px - ux * pz) / den, a = 1 - b - c;
      if (a < -eps || b < -eps || c < -eps) continue;
      out.push({ y: a * q[1] + b * q[4] + c * q[7], kind: q[9], rid: q[10] });
    }
    return out;
  };
  const latPt = (s, lat) => [s.x - Math.sin(s.h) * lat, s.y + Math.cos(s.h) * lat];
  const inTunnel = s => s.tunnel;   // under the lining / mountain: a ray from above legitimately hits roof or ground

  // 1 + 2: points every 2 m (samples) at lateral 0, +-half carriageway.
  { let n = 0, bad1 = 0, bad2 = 0, w1 = null, w2 = null;
    for (const r of roads) {
      const cw = RT[r.type].carriageway / 2;
      for (const s of r.samples) {
        if (inTunnel(s)) continue;
        for (const lat of [0, cw, -cw]) {
          const [x, y] = latPt(s, lat); n++;
          const hg = hAt(x, y), mk = masked(x, y);
          // check 1: first hit from 50 m above
          const hits = upHits(x, -y).map(h => ({ y: h.y, what: h.kind + '#' + h.rid, ok: h.kind === 'surface' }));
          if (!mk) hits.push({ y: hg + 0.001, what: 'terrain(unmasked)', ok: false });   // the +1 mm lets a road surface exactly at grade win over unmasked terrain
          if (hg <= T.water + 0.2) hits.push({ y: T.water, what: 'water', ok: false });
          hits.sort((a, b) => b.y - a.y);
          const top = hits[0];
          if (!top || !top.ok) { bad1++; const v = top ? top.y - s.z : 0; if (!w1 || v > w1.v) w1 = { x, y, road: r.id, what: top ? top.what : 'nothing', v }; }
          // check 2
          if (!mk && hg > s.z - 0.05) { bad2++; const v = hg - (s.z - 0.05); if (!w2 || v > w2.v) w2 = { x, y, road: r.id, what: 'terrain above road by', v }; }
        }
      }
    }
    done('road_surface_clear', w1, bad1, n, `${n} points, first hit always a road surface`);
    done('no_terrain_in_corridor', w2, bad2, n, `${n} points, terrain masked or >= 5 cm below road`);
  }

  // 3: wall height cap (from cross-sections: wall height = |ground - formation| at the wall face)
  { let n = 0, bad = 0, w = null, mx = 0;
    for (const r of roads) { const xs = P.crossSections(r); if (!xs) continue;
      r.samples.forEach((s, i) => { for (const k of ['L', 'R']) { const m = xs[i][k]; if (m.kind !== 'wall') continue; n++; const hgt = Math.abs(m.z1 - m.z0); mx = Math.max(mx, hgt);
        if (hgt > T.wallCap + 0.5) { bad++; const [x, y] = latPt(s, k === 'L' ? m.d1 : -m.d1); if (!w || hgt > w.v) w = { x, y, road: r.id, what: k, v: hgt }; } } }); }
    done('wall_height_cap', w, bad, n, `${n} wall sections, max height ${mx.toFixed(2)} m <= ${T.wallCap + 0.5}`); }

  // 4: slope meets ground (daylight z vs base terrain) and outer overlap row below terrain
  { let n = 0, bad = 0, w = null, miss = 0, wo = 0, bad2 = 0, w2 = null;
    for (const r of roads) { const xs = P.crossSections(r); if (!xs) continue;
      r.samples.forEach((s, i) => { for (const k of ['L', 'R']) { const m = xs[i][k]; if (m.kind !== 'slope' || m.d1 - m.edge < 0.05) continue;
        const sg = k === 'L' ? 1 : -1, [x, y] = latPt(s, sg * m.d1), e = upHits(x, -y).filter(h => h.kind === 'earthwork' && h.rid === r.id);
        if (!e.length) { miss++; continue; }
        n++; const dz = Math.abs(e[0].y - hAt(x, y));
        if (dz > 0.15) { bad++; if (!w || dz > w.v) w = { x, y, road: r.id, what: 'daylight dz', v: dz }; }
        const [xo, yo] = latPt(s, sg * (m.d1 + 1 - 0.002)), eo = upHits(xo, -yo).filter(h => h.kind === 'earthwork' && h.rid === r.id);
        if (eo.length) { wo++; const ex = eo[0].y - hAt(xo, yo); if (ex > 0) { bad2++; if (!w2 || ex > w2.v) w2 = { x: xo, y: yo, road: r.id, what: 'overlap row above terrain by', v: ex }; } }
      } }); }
    done('slope_meets_ground', w, bad, n, `${n} daylight points within 0.15 m (${miss} not found in mesh)`);
    done('slope_overlap_below', w2, bad2, wo, `${wo} overlap-row points all below terrain`); }

  // 5: piers/abutment bases (down-facing structure faces far below the deck near a bridge) reach below base terrain at every corner
  { let n = 0, bad = 0, w = null;
    for (const m of meshes) { if (kindOf(m) !== 'structure') continue; const r = m.userData.road; if (!r) continue;
      const pos = m.geometry.attributes.position.array, idx = m.geometry.index.array, S = r.samples; if (!S.some(s => s.bridge)) continue;
      for (let t = 0; t < idx.length; t += 3) {
        const v = [0, 1, 2].map(q => idx[t + q] * 3), y0 = pos[v[0] + 1];
        if (Math.abs(pos[v[1] + 1] - y0) > 1e-4 || Math.abs(pos[v[2] + 1] - y0) > 1e-4) continue;
        const ux = pos[v[1]] - pos[v[0]], uz = pos[v[1] + 2] - pos[v[0] + 2], vx = pos[v[2]] - pos[v[0]], vz = pos[v[2] + 2] - pos[v[0] + 2];
        if (uz * vx - ux * vz >= -1e-9) continue;   // keep down-facing only
        const cx = pos[v[0]], cy = -pos[v[0] + 2]; let bi = 0, bd = 1e18; S.forEach((s, i) => { const d = (s.x - cx) ** 2 + (s.y - cy) ** 2; if (d < bd) { bd = d; bi = i; } });
        let nearBr = false; for (let i = Math.max(0, bi - 8); i <= Math.min(S.length - 1, bi + 8); i++) if (S[i].bridge) nearBr = true;
        if (!nearBr || y0 > S[bi].z - DECK_T - 1.2) continue;   // not a foundation base
        for (const q of v) { n++; const x = pos[q], y = -pos[q + 2], ex = y0 - hAt(x, y); if (ex > 0) { bad++; if (!w || ex > w.v) w = { x, y, road: r.id, what: 'pier base above ground by', v: ex }; } }
      } }
    done('piers_grounded', w, bad, n, n ? `${n} pier/abutment base corners all at or below terrain` : 'WARNING none found (no bridge?)'); if (!n) results[results.length - 1].pass = false; }

  // 6: water only where the base terrain is not above water + 0.2 (replicates the water shader's bilinear height test vs the rendered triangle mesh)
  { const TN = 513, CELL = WORLD / (TN - 1), TH = P.TH, L = T.water + 0.2; let n = 0, bad = 0, w = null;
    for (let y = 0.5; y < WORLD; y += 1) for (let x = 0.5; x < WORLD; x += 1) {
      const gx = x / CELL, gy = y / CELL, i = Math.min(TN - 2, Math.floor(gx)), j = Math.min(TN - 2, Math.floor(gy)), fx = gx - i, fy = gy - j, k = j * TN + i;
      const bil = (TH[k] * (1 - fx) + TH[k + 1] * fx) * (1 - fy) + (TH[k + TN] * (1 - fx) + TH[k + TN + 1] * fx) * fy;
      if (bil > L) continue; n++;   // water fragment drawn here
      const ex = hAt(x, y) - L; if (ex > 0.02) { bad++; if (!w || ex > w.v) w = { x, y, road: 0, what: 'ground above water limit by', v: ex }; }
    }
    done('water_below_ground', w, bad, n, `${n} water fragments (1 m grid), none over ground > water+0.2`); }

  // 7: median fill only between same-type, opposite-direction, near-parallel dual pairs; vertices within gap+carriageway+3 m of the pair
  { let n = 0, bad = 0, w = null;
    for (const r of roads) { const xs = P.crossSections(r); if (!xs) continue;
      r.samples.forEach((s, i) => { for (const k of ['L', 'R']) { const m = xs[i][k]; if (m.kind !== 'median') continue; n++;
        const sg = k === 'L' ? 1 : -1, nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, lim = (RT[r.type].dual ? RT[r.type].gap : 0) + (RT[r.type] ? RT[r.type].carriageway : 0) + 3;
        let why = null, partner = null;
        if (!RT[r.type].dual) why = 'median on non-dual road';
        else { let bd = 1e18; for (const r2 of roads) { if (r2 === r || r2.type !== r.type) continue;
          for (const t of r2.samples) { const dx = t.x - s.x, dy = t.y - s.y, d = Math.hypot(dx, dy); if (d > lim + 4) continue; const lat = dx * nx + dy * ny; if (lat > 0 && Math.abs(d - lat) < 2 && d < bd) { bd = d; partner = { t, r2 }; } } }
          if (!partner) why = 'no paired road'; else if (Math.cos(partner.t.h - s.h) > -0.7) why = 'partner not opposite-direction near-parallel'; }
        if (!why) for (const d of [m.edge, m.mid, m.d1]) { const vx = s.x + nx * d, vy = s.y + ny * d;
          const dd = Math.min(Math.hypot(vx - s.x, vy - s.y), Math.hypot(vx - partner.t.x, vy - partner.t.y)); if (dd > lim) { why = 'vertex far from pair'; break; } }
        if (why) { bad++; if (!w) w = { x: s.x, y: s.y, road: r.id, what: why, v: 0 }; }
      } }); }
    done('median_only_for_pairs', w, bad, n, `${n} median sections, all between opposite-direction same-type pairs`, ''); }
  // 8: crossing_heights_match: wherever two centrelines cross or touch in plan, surface z differs by < 0.05 m (shared junction height) or >= 5.3 m (clears headroom)
  { let n = 0, bad = 0, w = null;
    const segX = (a, b, c, d) => { const rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y, den = rx * sy - ry * sx; if (Math.abs(den) < 1e-9) return null;
      const t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / den, u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / den, e = 1e-3;
      return t < -e || t > 1 + e || u < -e || u > 1 + e ? null : { t, u }; };
    for (let i = 0; i < roads.length; i++) for (let j = i + 1; j < roads.length; j++) {
      const A = roads[i].samples, B = roads[j].samples;
      for (let p = 0; p + 1 < A.length; p++) for (let q = 0; q + 1 < B.length; q++) {
        const h = segX(A[p], A[p + 1], B[q], B[q + 1]); if (!h) continue;
        const za = A[p].z + (A[p + 1].z - A[p].z) * h.t, zb = B[q].z + (B[q + 1].z - B[q].z) * h.u, dz = Math.abs(za - zb); n++;
        if (dz >= 0.05 && dz < 5.3) { bad++; if (!w || dz > w.v) w = { x: A[p].x + (A[p + 1].x - A[p].x) * h.t, y: A[p].y + (A[p + 1].y - A[p].y) * h.t, road: roads[i].id, what: 'dz vs road#' + roads[j].id, v: dz }; }
      } }
    done('crossing_heights_match', w, bad, n, `${n} plan crossings, all dz < 0.05 m or >= 5.3 m`); }
  // 8: no floating faces: wall/portal/earthwork vertices outside the road footprint must not stand above max(base terrain, slope/formation surface there) + 0.6 m (parapet allowance)
  { let n = 0, bad = 0, w = null; const CELL = 8, sh = new Map();
    for (const r of roads) r.samples.forEach((s, i) => { const key = Math.floor(s.x / CELL) * 100003 + Math.floor(s.y / CELL); let l = sh.get(key); if (!l) sh.set(key, l = []); l.push([r, i]); });
    const xsC = new Map(); const xsOf = r => { if (!xsC.has(r)) xsC.set(r, P.crossSections(r)); return xsC.get(r); };
    for (const m of meshes) { const kd = m.userData.kind; if (!(kd === 'wall' || kd === 'portal' || !kd)) continue; const r0 = m.userData.road; if (!r0) continue;
      const pos = m.geometry.attributes.position.array;
      for (let v = 0; v < pos.length; v += 3) {
        const x = pos[v], y = -pos[v + 2], z = pos[v + 1]; let best = null, bd = 1e18;
        const cx = Math.floor(x / CELL), cy = Math.floor(y / CELL);
        for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) { const l = sh.get((cx + a) * 100003 + cy + b); if (!l) continue; for (const [r, i] of l) { const s = r.samples[i], d = (s.x - x) ** 2 + (s.y - y) ** 2; if (d < bd) { bd = d; best = [r, i]; } } }
        if (!best) continue; const [r, i] = best, s = r.samples[i], hw = RT[r.type].carriageway / 2;
        const lat = (x - s.x) * -Math.sin(s.h) + (y - s.y) * Math.cos(s.h), al = Math.abs(lat);
        if (al <= hw + 0.4 + 0.5 || s.tunnel || s.bridge) continue; n++;
        let surf = s.z; const xs = xsOf(r), sec = xs && xs[i] && xs[i][lat > 0 ? 'L' : 'R'];
        if (sec && sec.kind === 'slope' && sec.d1 > sec.edge) { const t = Math.min(1, Math.max(0, (al - sec.edge) / (sec.d1 - sec.edge))); surf = sec.z0 + (sec.z1 - sec.z0) * t; } else if (sec) surf = sec.z0;
        const ex = z - (Math.max(hAt(x, y), surf) + 0.6);
        if (ex > 0.02) { (globalThis.__nf = globalThis.__nf || {})[kd || 'ew'] = ((globalThis.__nf || {})[kd || 'ew'] || 0) + 1; bad++; if (!w || ex > w.v) w = { x, y, road: r.id, what: (kd || 'earthwork') + ' vertex above surface by', v: ex }; }
      } }
    done('no_floating_faces', w, bad, n, `${n} outer vertices all within surface + 0.6 m`); }
  return results;
}
