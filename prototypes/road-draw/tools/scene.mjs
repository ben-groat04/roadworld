// Canonical test scene + fixed camera views for road-draw. Everyone checks the same hard cases.
//
//   node tools/scene.mjs <outDir> [viewName ...]      (default: all views)
//   INFO=1 node tools/scene.mjs <outDir> overview     (also prints per-feature structure stats)
//   node tools/scene.mjs --check                      (no screenshots: numeric invariant checks, one PASS/FAIL line each, exit 1 on any FAIL)
//
// Loads ../index.html (run `node build.mjs` first), draws the scene through window.__proto.drawRoad,
// closes help, hides the profile panel, then writes <outDir>/<view>.png at 1400x800.
// Prints console errors and exits 1 if there are any.
import { chromium } from 'playwright-core';
import { mkdirSync, readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const PAGE = pathToFileURL(resolve(HERE, '..', 'index.html')).href;

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

// 8. tight_curve_fill: residential U-turn (radius ~45 m) on gentle ground (~100 m, relief ~4 m) with the deck held at 110.5 m: fill ~8-12 m all round the bend,
//    so slopes from the two sides of the bend overlap on the inside. [x, y, z]
const TIGHT = { c: [1700, 880], R: 45, z: 110.5 };
// 9. tunnel_curve: residential road, CURVED (about 22 deg of bend inside the bore) and on a ~7% gradient (z a straight ramp 15.6 -> 29.5), through a spur: 71 m tunnel, portals found automatically.
const TUNNEL_CURVE = [[1831.5, 1834.5, 15.6], [1783.7, 1845.3, 19.1], [1734.7, 1843.4, 22.5], [1687.8, 1829, 26], [1646.2, 1803.1, 29.5]];
// 10. tunnel_short: straight, flat (z 93.7) 98 m tunnel under a thin ridge with open road beyond both portals: you can see daylight through it from the entry.
const TUNNEL_SHORT = [[1163.5, 743, 93.8], [1283.1, 663.4, 93.7], [1402.6, 583.7, 93.6]];
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
  tight_curve_fill: () => ({ target: [1668, 880], dist: 110, elev: 24, look: 160 }),
  tunnel_inside: () => ({ cam: info.portal.insideCam }),   // cam = { pos: [x, y, z], at: [x, y, z] }: scripted camera (no terrain following), inside the bore
  tunnel_curve_front: () => ({ target: info.tunnelCurve.xy, dist: TUNNEL_FRONT_DIST, elev: 8, look: info.tunnelCurve.heading }),
  tunnel_curve_inside: () => ({ cam: info.tunnelCurve.insideCam }),
  tunnel_short_through: () => ({ cam: info.tunnelShort.throughCam }),
  interchange: () => ({ target: [loaded.cx, loaded.cy], dist: Math.max(100, loaded.extent * 0.9), elev: 35, look: 90 }),   // sim_core scene (fixtures/interchange.json, else fixtures/sample.json); replaces the canonical scene, keep last
  overview: () => ({ target: [850, 850], dist: 2700, elev: 50, look: 100 }),
};
// ======================================================================================================================

let loaded = null;   // set by the interchange view after window.__inspect.load()
const CHECK_MODE = process.argv.includes('--check');
const optVal = k => { const i = process.argv.indexOf(k); return i < 0 ? null : process.argv[i + 1]; };
const CAM = optVal('--cam'), SCENARIO = optVal('--scenario') || 'canonical', CAM_NAME = optVal('--name') || 'cam';   // custom camera: --cam x,y,z,tx,ty,tz [--scenario canonical|interchange] [--name file]
const camNums = CAM ? CAM.split(',').map(Number) : null;
if (CAM && (camNums.length !== 6 || camNums.some(isNaN))) { console.error('--cam wants x,y,z,tx,ty,tz (world metres; z up)'); process.exit(2); }
if (!['canonical', 'interchange'].includes(SCENARIO)) { console.error('unknown --scenario ' + SCENARIO + '; have: canonical interchange'); process.exit(2); }
const argv = []; { const a = process.argv.slice(2); for (let i = 0; i < a.length; i++) { if (a[i] === '--check') continue; if (['--cam', '--scenario', '--name', '--eval'].includes(a[i])) { i++; continue; } argv.push(a[i]); } }
const outDir = resolve(argv[0] || '.');
const wanted = argv.slice(1);
const names = CAM || optVal('--eval') ? [] : wanted.length ? wanted : Object.keys(VIEWS);
for (const n of names) if (!VIEWS[n]) { console.error('unknown view ' + n + '; have: ' + Object.keys(VIEWS).join(' ')); process.exit(2); }
mkdirSync(outDir, { recursive: true });

const t0 = Date.now();
// Prefer an installed Edge/Chrome (no browser download); fall back to Playwright's own chromium if present.
// Software GL flags are Linux-only (no GPU in cloud boxes); on Windows/macOS the real GPU is used.
const launchArgs = process.platform === 'linux' ? ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : process.platform === 'win32' ? ['--use-angle=d3d11', '--force_high_performance_gpu'] : [];   // win32: steer onto the discrete GPU
let browser = null, browserName = null; const launchErrs = [];
for (const opt of [{ channel: 'msedge' }, { channel: 'chrome' }, {}]) {
  try { browser = await chromium.launch({ ...opt, args: launchArgs }); browserName = opt.channel; break; } catch (e) { launchErrs.push((opt.channel || 'bundled chromium') + ': ' + String(e.message).split('\n')[0]); }
}
if (!browser) { console.error('no usable browser (need Edge, Chrome, or playwright chromium already installed):\n' + launchErrs.join('\n')); process.exit(4); }
console.error('browser: ' + (browserName || 'chromium') + ' ' + browser.version());
const page = await browser.newPage({ viewport: { width: 1400, height: 800 } });
const errors = [];
page.on('console', m => { if (m.type() === 'error') errors.push('console.error: ' + m.text()); });
page.on('pageerror', e => errors.push('pageerror: ' + e.message));
await page.goto(PAGE);
try { await page.waitForFunction(() => window.__proto); } catch (e) { console.error('window.__proto never appeared; page errors (src/ likely broken, run node build.mjs and check):\n' + ([...new Set(errors)].join('\n') || '(none)')); await browser.close(); process.exit(3); }

// Close help, hide profile panel (the key handler toggles state; profile is on by default).
await page.evaluate(() => { const P = window.__proto; if (P.state.help) document.getElementById('help').click(); });
if (await page.evaluate(() => window.__proto.state.profile)) await page.keyboard.press('p');
await page.evaluate(() => { for (const id of ['hud', 'tune']) { const e = document.getElementById(id); if (e) e.style.display = 'none'; } });

// Draw the scene in the page. Returns derived geometry used by the views.
const info = await page.evaluate(({ CROSS, TEE, HILL, VIADUCT, TUNNEL, DUAL, MOTORWAY, TIGHT, TUNNEL_CURVE, TUNNEL_SHORT }) => {
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
    const j = S.length - 1 - [...S].reverse().findIndex(q => q.tunnel), at = (q, up) => [S[q].x, S[q].y, S[q].z + up];
    out.portal = { xy: [s.x, s.y], heading: deg(s.h), found: i > 0, insideCam: { pos: at(Math.min(k + 6, j), 1.7), at: at(Math.min(k + 30, j), 1.5) } }; }
  // 6
  { const pts = [], a0 = DUAL.a0 * Math.PI / 180, a1 = DUAL.a1 * Math.PI / 180;
    for (let k = 0; k < DUAL.n; k++) { const a = a0 + (a1 - a0) * k / (DUAL.n - 1); pts.push([DUAL.cc[0] + DUAL.R * Math.cos(a), DUAL.cc[1] + DUAL.R * Math.sin(a)]); }
    draw('dual_curve (centre+2 carriageways)', 3, pts);
    const am = (a0 + a1) / 2; out.dualMid = [DUAL.cc[0] + DUAL.R * Math.cos(am), DUAL.cc[1] + DUAL.R * Math.sin(am)];
    out.dualHeading = deg(am + Math.PI / 2); }
  // 7
  draw('motorway', 4, MOTORWAY);
  // 8
  { const c = TIGHT.c, R = TIGHT.R, z = TIGHT.z, pts = [[c[0] + 60, c[1] - R, z], [c[0] + 20, c[1] - R, z]];
    for (const a of [-135, -180, -225]) pts.push([c[0] + R * Math.cos(a * Math.PI / 180), c[1] + R * Math.sin(a * Math.PI / 180), z]);
    pts.push([c[0] + 20, c[1] + R, z], [c[0] + 60, c[1] + R, z]); pts.splice(2, 0, [c[0], c[1] - R, z]); pts.splice(6, 0, [c[0], c[1] + R, z]);
    draw('tight_curve_fill', 1, pts); }
  // 9, 10: further tunnels (curved + gradient; short straight)
  { const ext = (name, pts) => { const [r] = draw(name, 1, pts), S = r ? r.samples : [], i = S.findIndex(q => q.tunnel), j = S.length - 1 - [...S].reverse().findIndex(q => q.tunnel), at = (k, up) => [S[k].x, S[k].y, S[k].z + up];
      return { found: i > 0, heading: deg(S[Math.max(i, 0)].h), xy: [S[Math.max(i, 0)].x, S[Math.max(i, 0)].y], nTu: j - i + 1, insideCam: { pos: at(Math.min(i + 6, j), 1.7), at: at(Math.min(i + 30, j), 1.5) }, throughCam: { pos: at(Math.max(0, i - 17), 1.7), at: at(Math.min(S.length - 1, j + 12), 1.5) } }; };
    out.tunnelCurve = ext('tunnel_curve', TUNNEL_CURVE); out.tunnelShort = ext('tunnel_short', TUNNEL_SHORT); }
  P.flushGrade();
  out.stats = stats; out.roadCount = P.roads().length;
  return out;
}, { CROSS, TEE, HILL, VIADUCT, TUNNEL, DUAL, MOTORWAY, TIGHT, TUNNEL_CURVE, TUNNEL_SHORT });

if (CHECK_MODE) {
  const results = await page.evaluate(CHECKS_IN_PAGE);
  // terrain_covers_view: for every fixed canonical view, each screen sample whose ray lands on the graded world must show rendered pixels, never the void
  { const bad = [];
    for (const name of Object.keys(VIEWS)) {
      if (name === 'interchange') continue;
      const v = VIEWS[name]();
      if (v.cam) continue;   // scripted cameras inside a bore: the world is not visible from there
      const r = await page.evaluate(({ v, name }) => {
        const P = window.__proto, az = Math.atan2(-Math.cos(v.look * Math.PI / 180), Math.sin(v.look * Math.PI / 180)), W = 2048;
        P.setView(v.target[0], v.target[1], v.dist, az, (90 - v.elev) * Math.PI / 180); P.renderNow();
        const scene = P.scene, cam = P.camera, THREE = P.THREE, cv = document.getElementById('view'), gl = cv.getContext('webgl2');
        const kids = scene.children.map(c => [c, c.visible]), bg = scene.background, fogOn = scene.fog;
        const px = (x, y) => { const o = new Uint8Array(4); gl.readPixels(x, y, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, o); return o; };
        const bw = gl.drawingBufferWidth, bh = gl.drawingBufferHeight;
        // reference "void" pixel: everything hidden, flat sentinel background
        for (const c of scene.children) c.visible = false; scene.background = new THREE.Color(0xff00ff);
        P.renderNow(); const ref = px(5, 5);
        for (const [c, vis] of kids) c.visible = vis;
        // the dark apron and sky dome would hide a void: hide them, keep everything real
        for (const c of scene.children) if (c.isMesh && ((c.geometry.type === 'PlaneGeometry' && c.geometry.parameters.width > 10000) || c.renderOrder === -10)) c.visible = false;
        P.renderNow();
        const cams = cam.position.clone(), n = new THREE.Vector3(), d = new THREE.Vector3();
        let tested = 0, voidN = 0, first = null;
        for (let j = 1; j < 24; j++) for (let i = 1; i < 40; i++) {
          const sx = i / 40, sy = j / 24; n.set(sx * 2 - 1, 1 - sy * 2, 0.5).unproject(cam); d.copy(n).sub(cams).normalize();   // three: x east, y up, z = -north
          let hit = null, entered = false;
          for (let k = 2; k < 14000; k += 2) { const x = cams.x + d.x * k, y = -(cams.z + d.z * k), z = cams.y + d.y * k;
            if (x < 3 || x > W - 3 || y < 3 || y > W - 3) continue;   // only samples on the world proper (edge skirt excluded)
            if (z <= P.groundAt(x, y)) { if (entered) hit = [x, y]; break; }   // a ray already below ground on entering the world hit the edge skirt / outside, not the world surface
            entered = true; }
          if (!hit) continue; tested++;
          const o = px(Math.floor(sx * bw), Math.floor((1 - sy) * bh));
          if (Math.abs(o[0] - ref[0]) + Math.abs(o[1] - ref[1]) + Math.abs(o[2] - ref[2]) < 6) { voidN++; if (!first) first = hit; }
        }
        for (const [c, vis] of kids) c.visible = vis; scene.background = bg; P.renderNow();
        return { name, tested, voidN, first, ref: Array.from(ref) };
      }, { v, name });
      if (r.voidN) bad.push(`${r.name}: ${r.voidN}/${r.tested} samples void, first at (${r.first[0].toFixed(0)},${r.first[1].toFixed(0)})`);
      else if (r.tested < 20) bad.push(`${r.name}: only ${r.tested} samples hit the world (test not meaningful)`);
    }
    results.push({ name: 'terrain_covers_view', pass: bad.length === 0, msg: bad.length ? bad.join(' | ') : `all ${Object.keys(VIEWS).length - 1} fixed views: every screen ray that lands on the world shows rendered ground/structure, never void` }); }
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
  // perf2: JS ms per frame for 60 simulated mousemoves (static camera, mid-stroke so the preview is live) and for 60 camera-orbit frames with
  // the pointer parked over the scene, split into the F-readout buckets. GPU time is meaningless under swiftshader, JS time is valid.
  // Pass: pick+snap+preview+shadow+clip < 4 ms/frame (renderer.render reported but excluded: its JS side is driver dependent) and <= 2 shadow-map redraws.
  { const pf = await page.evaluate(() => {
      const P = window.__proto, pf = window.__perf, PF = pf.pf, c = P.camera, tg = P.controls.target, cv = document.getElementById('view'), st = P.state;
      const out = { gpu: PF.gpu }; let now = performance.now() + 1000;
      P.setView(1080, 200, 350, 0.4, 1.0); P.renderNow();
      st.mode = 'draw'; st.type = 2; st.oneWay = false; st.shift = false;
      const z0 = P.hAt(1000, 200); st.draw = { pts: [{ x: 1000, y: 200, z: z0 }], hs: null, attach: ['free'] };
      cv.dispatchEvent(new PointerEvent('pointerenter', { bubbles: true }));
      const rc = cv.getBoundingClientRect(), mv = i => { const a = i / 59; window.dispatchEvent(new PointerEvent('pointermove', { clientX: rc.left + 250 + 900 * a, clientY: rc.top + 300 + 150 * Math.sin(a * 9), bubbles: true })); };
      const orbit = () => { const rx = c.position.x - tg.x, rz = c.position.z - tg.z, a = 0.02, co = Math.cos(a), si = Math.sin(a);
        c.position.x = tg.x + rx * co - rz * si; c.position.z = tg.z + rx * si + rz * co; P.controls.update(); };
      const run = (name, body) => {
        for (let i = 0; i < 6; i++) { body(i % 2 ? 0 : 1); now += 16.7; pf.step(now); }   // warm-up (JIT, shader compile, first shadow map)
        const t0 = {}, n0 = PF.ticks, sh0 = PF.shadowPasses; for (const k of PF.names) t0[k] = PF.tot[k];
        for (let i = 0; i < 60; i++) { body(i); now += 16.7; pf.step(now); }
        const r = { shadowPasses: PF.shadowPasses - sh0 }, n = PF.ticks - n0; let js = 0;
        for (const k of PF.names) { r[k] = (PF.tot[k] - t0[k]) / n; if (k !== 'render') js += r[k]; }
        r.js = js; out[name] = r;
      };
      run('mouse', i => mv(i));
      mv(30);   // park the pointer mid-screen
      run('camera', () => orbit());
      return out;
    });
    const f = r => ['pick', 'snap', 'preview', 'shadow', 'clip', 'render'].map(k => k + ' ' + r[k].toFixed(2)).join(' | ');
    for (const k of ['mouse', 'camera']) results.push({ name: 'perf_js_' + k, pass: pf[k].js < 4 && pf[k].shadowPasses <= 2, msg: `${pf[k].js.toFixed(2)} ms JS/frame (target < 4, excl. render): ${f(pf[k])} | shadow maps redrawn ${pf[k].shadowPasses}/60` });
    results.push({ name: 'perf_gpu', pass: true, msg: `renderer: ${pf.gpu}` }); }
  let bad = 0;
  for (const r of results) { if (!r.pass) bad++; console.log(`${r.pass ? 'PASS' : 'FAIL'} ${r.name.padEnd(24)} ${r.msg}`); }
  await browser.close();
  console.log(`${results.length - bad}/${results.length} passed in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
  process.exit(bad ? 1 : 0);
}
if (process.env.INFO) console.log(JSON.stringify(info, null, 1));
if (!info.portal.found) errors.push('scene: no tunnel portal found (tunnel road has no tunnel samples)');

for (const name of names.sort((a, b) => (a === 'interchange') - (b === 'interchange'))) {
  if (name === 'interchange') {
    const fx = ['interchange.json', 'sample.json'].map(f => resolve(HERE, '..', 'fixtures', f)).find(existsSync);
    loaded = await page.evaluate(j => { const i = window.__inspect.load(j, 'fixture'); return { cx: i.cx, cy: i.cy, extent: i.extent }; }, readFileSync(fx, 'utf8'));
  }
  const v = VIEWS[name]();
  await page.evaluate(({ v }) => {
    const P = window.__proto;
    if (v.cam) { P.freeCam(v.cam.pos, v.cam.at); P.renderNow(); return; }   // scripted camera (inside a bore)
    const az = Math.atan2(-Math.cos(v.look * Math.PI / 180), Math.sin(v.look * Math.PI / 180));
    P.setView(v.target[0], v.target[1], v.dist, az, (90 - v.elev) * Math.PI / 180);
    P.renderNow();
  }, { v });
  await page.waitForTimeout(250);   // let the main loop apply updateCamera (terrain-following) before the final render
  await page.evaluate(() => window.__proto.renderNow());
  await page.screenshot({ path: resolve(outDir, name + '.png') });
}

if (optVal('--eval')) { const src = readFileSync(resolve(optVal('--eval')), 'utf8'); console.log(JSON.stringify(await page.evaluate(`(async () => { const P = window.__proto; ${src}
})()`))); await browser.close(); process.exit(0); }   // debugging: --eval file.js runs the file body in the page (P = window.__proto; use `return`) after the scene is drawn and prints the JSON result
if (CAM) {   // custom camera: the target is moved along the view ray onto the ground so the terrain-following camera loop does not shift the view
  if (SCENARIO === 'interchange') { const fx = ['interchange.json', 'sample.json'].map(f => resolve(HERE, '..', 'fixtures', f)).find(existsSync); await page.evaluate(j => window.__inspect.load(j, 'fixture'), readFileSync(fx, 'utf8')); }
  const camInfo = await page.evaluate(({ c }) => {
    const P = window.__proto, [px, py, pz, tx, ty, tz] = c, L = Math.hypot(tx - px, ty - py, tz - pz) || 1, d = [(tx - px) / L, (ty - py) / L, (tz - pz) / L];
    let t = L; for (let k = 1; k < 8000; k += 1) { if (pz + d[2] * k <= P.hAt(px + d[0] * k, py + d[1] * k)) { t = k; break; } }
    const gx = px + d[0] * t, gy = py + d[1] * t, gz = P.hAt(gx, gy), dist = Math.hypot(gx - px, gy - py, gz - pz) || 1;
    const polar = Math.atan2(Math.hypot(px - gx, py - gy), pz - gz), az = Math.atan2(px - gx, -(py - gy));
    P.setView(gx, gy, dist, az, Math.min(Math.max(polar, 0.05), 1.55)); P.renderNow();
    return { ground: [gx, gy, gz], camera: [P.camera.position.x, -P.camera.position.z, P.camera.position.y] };
  }, { c: camNums });
  console.log('cam: looking at ground (' + camInfo.ground.map(v => v.toFixed(1)).join(', ') + '), camera at (' + camInfo.camera.map(v => v.toFixed(1)).join(', ') + ')');
  await page.waitForTimeout(250); await page.evaluate(() => window.__proto.renderNow());
  await page.screenshot({ path: resolve(outDir, CAM_NAME + '.png') }); names.push(CAM_NAME);
}
await browser.close();
const secs = ((Date.now() - t0) / 1000).toFixed(1);
console.log(`wrote ${names.length} view(s) to ${outDir} in ${secs}s: ${names.join(' ')}`);
if (errors.length) { console.error(errors.length + ' error(s):\n' + [...new Set(errors)].join('\n')); process.exit(1); }

// ============================== numeric invariant checks (run inside the page; must be self-contained) ==============================
// Ground is ONE graded height grid: P.groundAt(x,y) (GH, 0.5 m). P.hBase / P.terrainH = original terrain. Roads are only structures now.
// Meshes carry userData.kind (roadmesh.js): surface | wall | pier | deck | abutment | parapet | portal | lining (terrain/water/skirt live outside the road group).
// Ground and water are analytic (not raycast): groundAt is what the clipmap terrain draws.
function CHECKS_IN_PAGE() {
  const P = window.__proto, THREE = P.THREE, T = P.T, RT = P.ROAD_TYPES, DECK_T = 1.5;
  const results = [];
  const missing = ['groundAt', 'GH', 'GRID_RES', 'wallRuns', 'hBase', 'ROAD_TYPES', 'T'].filter(k => P[k] == null);
  if (missing.includes('groundAt') || missing.includes('wallRuns')) return [{ name: 'setup', pass: false, msg: 'window.__proto lacks ' + missing.join(', ') }];
  if (missing.length) results.push({ name: 'proto_exports', pass: false, msg: 'window.__proto lacks ' + missing.join(', ') + ' (fell back: hBase -> bilinear of P.TH)' });
  const groundAt = P.groundAt, wallRuns = P.wallRuns, terrainH = P.terrainH || groundAt;
  const hBase = P.hBase || ((x, y) => { const N = 513, C = 2048 / (N - 1), gx = Math.min(N - 1.0001, Math.max(0, x / C)), gy = Math.min(N - 1.0001, Math.max(0, y / C)), i = Math.floor(gx), j = Math.floor(gy), fx = gx - i, fy = gy - j, k = j * N + i, H = P.TH;
    return (H[k] * (1 - fx) + H[k + 1] * fx) * (1 - fy) + (H[k + N] * (1 - fx) + H[k + N + 1] * fx) * fy; });
  P.renderNow();   // builds road meshes (and flushes grading)
  const fmt = (x, y) => `(${x.toFixed(1)},${y.toFixed(1)})`;
  const done = (name, worst, count, total, okMsg, unit = 'm') => results.push({ name, pass: count === 0, msg: count === 0 ? okMsg : `${count}/${total} bad; worst ${worst.what} at ${fmt(worst.x, worst.y)} road#${worst.road}${unit ? ' ' + worst.v.toFixed(3) + unit : ''}` });
  const roads = P.roads().filter(r => r.samples);
  const roadGroup = P.scene.children.find(g => g.isGroup && (() => { let f = false; g.traverse(o => { if (o.userData && o.userData.road) f = true; }); return f; })());
  if (!roadGroup) return [{ name: 'setup', pass: false, msg: 'no road group in scene' }];
  const isBr = s => !!(s.bridge || s.br), isTu = s => !!(s.tunnel || s.tu);
  const latPt = (s, lat) => [s.x - Math.sin(s.h) * lat, s.y + Math.cos(s.h) * lat];
  const track = (w, o) => (!w || o.v > w.v) ? o : w;

  // ---- spatial grid over every road mesh triangle (three coords: X = x, Z = -y) ----
  const CS = 4, grid = new Map(), tris = [];   // tris: [ax,ay,az,bx,by,bz,cx,cy,cz,kind,roadId]
  const STRUCT = new Set(['wall', 'pier', 'deck', 'abutment', 'parapet', 'portal', 'lining']);
  const kindOf = o => { const k = o.userData.kind; return !k ? 'other' : STRUCT.has(k) ? 'structure' : 'surface'; };
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
  const upHits = (X, Z, eps = 2e-3) => {   // up-facing triangle hits at a plan point: [{y, kind, rid}]
    const l = grid.get(Math.floor(X / CS) * 100003 + Math.floor(Z / CS)), out = [];
    if (!l) return out;
    for (const id of l) {
      const q = tris[id], ux = q[3] - q[0], uz = q[5] - q[2], vx = q[6] - q[0], vz = q[8] - q[2], den = uz * vx - ux * vz;
      if (den <= 1e-9) continue;
      const px = X - q[0], pz = Z - q[2], b = (pz * vx - px * vz) / den, c = (uz * px - ux * pz) / den, a = 1 - b - c;
      if (a < -eps || b < -eps || c < -eps) continue;
      out.push({ y: a * q[1] + b * q[4] + c * q[7], kind: q[9], rid: q[10] });
    }
    return out;
  };
  // station hash for "nearest road station" queries
  const SC = 8, sh = new Map();
  for (const r of roads) r.samples.forEach((s, i) => { const key = Math.floor(s.x / SC) * 100003 + Math.floor(s.y / SC); let l = sh.get(key); if (!l) sh.set(key, l = []); l.push([r, i]); });
  const nearStation = (x, y) => { let best = null, bd = 1e18; const cx = Math.floor(x / SC), cy = Math.floor(y / SC);
    for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) { const l = sh.get((cx + a) * 100003 + cy + b); if (!l) continue; for (const [r, i] of l) { const s = r.samples[i], d = (s.x - x) ** 2 + (s.y - y) ** 2; if (d < bd) { bd = d; best = [r, i]; } } }
    return best; };
  // is another road's footprint/reach (excluding road r) within `reach` m of (x,y)?
  const otherRoadNear = (x, y, r, reach) => { const cx = Math.floor(x / SC), cy = Math.floor(y / SC), n = Math.ceil(reach / SC) + 1;
    for (let a = -n; a <= n; a++) for (let b = -n; b <= n; b++) { const l = sh.get((cx + a) * 100003 + cy + b); if (!l) continue;
      for (const [r2, i] of l) { if (r2 === r) continue; const s = r2.samples[i]; if ((s.x - x) ** 2 + (s.y - y) ** 2 < (reach + RT[r2.type].total / 2) ** 2) return true; } }
    return false; };

  // road_surface_clear: points every 2 m at lateral 0, +-half carriageway. First downward hit is a road surface mesh (never wall/portal/pier/deck),
  // and the analytic ground must be >= 5 cm below the road there (ground that is higher would hide the road, or be the first hit).
  { let n = 0, bad = 0, w = null;
    for (const r of roads) {
      const cw = RT[r.type].carriageway / 2;
      for (const s of r.samples) {
        if (isTu(s)) continue;   // under the lining / mountain: a ray from above legitimately hits roof or ground
        for (const lat of [0, cw, -cw]) {
          const [x, y] = latPt(s, lat); n++;
          const hg = groundAt(x, y);
          const hits = upHits(x, -y).map(h => ({ y: h.y, what: h.kind + '#' + h.rid, ok: h.kind === 'surface' }));
          hits.push({ y: hg + 0.05, what: 'ground (within 5 cm of road)', ok: false });   // ground counts as hit unless >= 5 cm below the surface
          if (hg <= T.water + 0.2) hits.push({ y: T.water, what: 'water', ok: false });
          hits.sort((a, b) => b.y - a.y);
          const top = hits[0];
          if (!top || !top.ok) { bad++; w = track(w, { x, y, road: r.id, what: top ? top.what : 'nothing', v: top ? top.y - s.z : 0 }); }
        }
      }
    }
    done('road_surface_clear', w, bad, n, `${n} points, first hit always a road surface, ground >= 5 cm below`); }

  // ground_under_road: every non-bridge, non-tunnel station, across the whole footprint: road - 0.6 <= groundAt <= road - 0.05
  { let n = 0, bad = 0, w = null, hi = 0, lo = 0, nearEnd = 0, known = 0; const roadsBad = {};
    for (const r of roads) { const hw = RT[r.type].total / 2;
      r.samples.forEach((s, si) => { if (isBr(s) || isTu(s)) return;
        let nearSt = 99; for (let k = -6; k <= 6; k++) { const q = r.samples[si + k]; if (q && (isBr(q) || isTu(q))) nearSt = Math.min(nearSt, Math.abs(k)); }
        for (let lat = -hw; lat <= hw + 1e-6; lat += 0.5) { const l = Math.min(hw - 0.01, Math.max(-hw + 0.01, lat)), [x, y] = latPt(s, l); n++;
          const d = s.z - groundAt(x, y);   // want 0.05 <= d <= 0.6
          if (d < 0.05 - 1e-4 || d > 0.6 + 1e-4) { if (otherRoadNear(x, y, r, 6)) { known++; return; } bad++; const v = d < 0.05 ? 0.05 - d : d - 0.6; if (d < 0.05) hi++; else lo++; if (nearSt <= 6) nearEnd++; roadsBad[r.id] = (roadsBad[r.id] || 0) + 1; w = track(w, { x, y, road: r.id, what: (d < 0.05 ? 'ground pokes through road by' : 'gap under road edge') + ' (' + (nearSt === 99 ? 'far from' : nearSt + ' stations from') + ' bridge/tunnel) ', v }); } } }); }
    done('ground_under_road', w, bad, n, `${n} footprint points, ground 5..60 cm below road`); if (known) results[results.length - 1].msg += ` ; KNOWN junction_zone: ${known} points within (other road half-width + 6 m) of another road (pending sim_core junction generator)`; if (bad) results[results.length - 1].msg += ` (poke-through ${hi}, gaps ${lo}, ${nearEnd} within 6 stations of bridge/tunnel, by road ${JSON.stringify(roadsBad)})`; }

  // slopes_daylight: unwalled sides, walking outward from the footprint edge. Never overshoot the original ground (cut: above, fill: below) by > 0.3 m,
  // and reach it (within 0.15 m) by the batter reach. Reference = P.terrainH (the exact base function GH is graded against). Skipped near other roads.
  { let n = 0, bad = 0, w = null, nb = 0, ov = 0, nc = 0;
    for (const r of roads) { const hw = RT[r.type].total / 2, reach = hw + T.wallStart * T.batter + 2;
      r.samples.forEach((s, i) => { if (isBr(s) || isTu(s)) return;
        for (const side of ['L', 'R']) { if (side === 'L' ? s.wL : s.wR) continue;
          const sg = side === 'L' ? 1 : -1; let skip = false, worst = 0, wp = null;
          for (let d = hw + 0.5; d <= reach + 4; d += 0.5) { const [x, y] = latPt(s, sg * d); if (otherRoadNear(x, y, r, T.wallStart * T.batter + 2)) { skip = true; break; }   // inside another road's footprint or slope reach (junction interaction)
           
            const b = terrainH(x, y), g = groundAt(x, y), cut = b > s.z - 0.15, over = cut ? g - b : b - g;
            if (over > worst) { worst = over; wp = [x, y]; }
            if (d >= reach + 3.5 && Math.abs(g - b) > 0.15 && !skip) { nc++; bad++; w = track(w, { x, y, road: r.id, what: 'not converged to base by', v: Math.abs(g - b) }); skip = 'counted'; } }
          if (skip === true) continue; n++;
          if (worst > 0.3) { ov++; bad++; w = track(w, { x: wp[0], y: wp[1], road: r.id, what: 'overshoots base by', v: worst }); } } }); }
    done('slopes_daylight', w, bad, n, `${n} unwalled side profiles reach base ground, max overshoot <= 0.3 m`); if (bad) results[results.length - 1].msg += ` (overshoot ${ov}, unconverged ${nc})`; }

  // median_flat: between the two carriageways of each dual pair, groundAt has no step > 0.5 m between adjacent 0.5 m texels
  { let n = 0, bad = 0, w = null, pairs = 0;
    for (const r of roads) { if (!RT[r.type].dual) continue;
      for (const s of r.samples) { if (isBr(s) || isTu(s)) continue;
        const nx = -Math.sin(s.h), ny = Math.cos(s.h), lim = RT[r.type].gap + RT[r.type].carriageway + 3; let partner = null, bd = 1e18, sgn = 1;
        for (const r2 of roads) { if (r2 === r || r2.type !== r.type) continue;
          for (const t of r2.samples) { const dx = t.x - s.x, dy = t.y - s.y, d = Math.hypot(dx, dy); if (d > lim) continue; const lat = dx * nx + dy * ny; if (Math.abs(d - Math.abs(lat)) < 2 && d < bd && Math.cos(t.h - s.h) < -0.7) { bd = d; partner = t; sgn = lat > 0 ? 1 : -1; } } }
        if (!partner) continue; pairs++;
        const L = Math.hypot(partner.x - s.x, partner.y - s.y); let prev = null;
        for (let d = 0; d <= L + 1e-6; d += 0.5) { const [x, y] = latPt(s, sgn * d), g = groundAt(x, y); if (prev != null) { n++; const st = Math.abs(g - prev); if (st > 0.5) { bad++; w = track(w, { x, y, road: r.id, what: 'step between texels', v: st }); } } prev = g; } } }
    done('median_flat', w, bad, n, pairs ? `${n} texel pairs over ${pairs} dual stations, max step <= 0.5 m` : 'WARNING no dual pairs found'); if (!pairs) results[results.length - 1].pass = false; }

  // wall_height_cap: from wallRuns, zTop - zBot
  { let n = 0, bad = 0, w = null, mx = 0;
    for (const r of roads) for (const run of wallRuns(r)) for (const p of run.pts) { n++; const hgt = p.zTop - p.zBot; mx = Math.max(mx, hgt);
      if (hgt > T.wallCap + 0.5) { bad++; w = track(w, { x: p.x, y: p.y, road: r.id, what: run.side + (run.cut ? ' cut wall' : ' fill wall'), v: hgt }); } }
    done('wall_height_cap', w, bad, n, `${n} wall stations, max height ${mx.toFixed(2)} m <= ${T.wallCap + 0.5}`); }

  // piers_grounded: down-facing foundation bases (far below the deck near a bridge) are at or below the original ground at every corner
  { let n = 0, bad = 0, w = null;
    for (const m of meshes) { if (kindOf(m) !== 'structure') continue; const r = m.userData.road; if (!r) continue;
      const pos = m.geometry.attributes.position.array, idx = m.geometry.index.array, S = r.samples; if (!S.some(isBr)) continue;
      for (let t = 0; t < idx.length; t += 3) {
        const v = [0, 1, 2].map(q => idx[t + q] * 3), y0 = pos[v[0] + 1];
        if (Math.abs(pos[v[1] + 1] - y0) > 1e-4 || Math.abs(pos[v[2] + 1] - y0) > 1e-4) continue;
        const ux = pos[v[1]] - pos[v[0]], uz = pos[v[1] + 2] - pos[v[0] + 2], vx = pos[v[2]] - pos[v[0]], vz = pos[v[2] + 2] - pos[v[0] + 2];
        if (uz * vx - ux * vz >= -1e-9) continue;   // keep down-facing only
        const cx = pos[v[0]], cy = -pos[v[0] + 2]; let bi = 0, bd = 1e18; S.forEach((s, i) => { const d = (s.x - cx) ** 2 + (s.y - cy) ** 2; if (d < bd) { bd = d; bi = i; } });
        let nearBr = false; for (let i = Math.max(0, bi - 8); i <= Math.min(S.length - 1, bi + 8); i++) if (isBr(S[i])) nearBr = true;
        if (!nearBr || y0 > S[bi].z - DECK_T - 1.2) continue;   // not a foundation base
        for (const q of v) { n++; const x = pos[q], y = -pos[q + 2], ex = y0 - hBase(x, y); if (ex > 0) { bad++; w = track(w, { x, y, road: r.id, what: 'pier base above ground by', v: ex }); } }
      } }
    done('piers_grounded', w, bad, n, n ? `${n} pier/abutment base corners all at or below base terrain` : 'WARNING none found (no bridge?)'); if (!n) results[results.length - 1].pass = false; }

  // water_below_ground: the water shader draws wherever groundAt <= water+0.2. (a) never over a road formation sitting above that limit;
  // (b) graded ground may only reach into the water limit within 40 m of a road station (no unexplained holes in the base terrain).
  { const L = T.water + 0.2; let n = 0, bad = 0, w = null;
    for (const r of roads) for (const s of r.samples) { if (isBr(s) || isTu(s) || s.z - 0.15 <= L + 0.05) continue; n++;
      const g = groundAt(s.x, s.y); if (g <= L) { bad++; w = track(w, { x: s.x, y: s.y, road: r.id, what: 'water drawn over road formation at z=' + s.z.toFixed(1) + ', ground', v: L - g + 1e-3 }); } }
    for (let y = 0.5; y < 2048; y += 1) for (let x = 0.5; x < 2048; x += 1) {
      if (groundAt(x, y) > L) continue; n++;   // water fragment drawn here
      const ex = hBase(x, y) - L; if (ex > 0.5) { const nb = nearStation(x, y); if (!nb || (nb[0].samples[nb[1]].x - x) ** 2 + (nb[0].samples[nb[1]].y - y) ** 2 > 40 * 40) { bad++; w = track(w, { x, y, road: 0, what: 'water over base ground above limit by', v: ex }); } } }
    done('water_below_ground', w, bad, n, `${n} water fragments / road stations, none over ground or road above water+0.2`); }

  // crossing_heights_match: wherever two centrelines cross or touch in plan, surface z differs by < 0.05 m (shared junction height) or >= 5.3 m (clears headroom)
  { let n = 0, bad = 0, w = null;
    const segX = (a, b, c, d) => { const rx = b.x - a.x, ry = b.y - a.y, sx = d.x - c.x, sy = d.y - c.y, den = rx * sy - ry * sx; if (Math.abs(den) < 1e-9) return null;
      const t = ((c.x - a.x) * sy - (c.y - a.y) * sx) / den, u = ((c.x - a.x) * ry - (c.y - a.y) * rx) / den, e = 1e-3;
      return t < -e || t > 1 + e || u < -e || u > 1 + e ? null : { t, u }; };
    for (let i = 0; i < roads.length; i++) for (let j = i + 1; j < roads.length; j++) {
      const A = roads[i].samples, B = roads[j].samples;
      for (let p = 0; p + 1 < A.length; p++) for (let q = 0; q + 1 < B.length; q++) {
        const h = segX(A[p], A[p + 1], B[q], B[q + 1]); if (!h) continue;
        const za = A[p].z + (A[p + 1].z - A[p].z) * h.t, zb = B[q].z + (B[q + 1].z - B[q].z) * h.u, dz = Math.abs(za - zb); n++;
        if (dz >= 0.05 && dz < 5.3) { bad++; w = track(w, { x: A[p].x + (A[p + 1].x - A[p].x) * h.t, y: A[p].y + (A[p + 1].y - A[p].y) * h.t, road: roads[i].id, what: 'dz vs road#' + roads[j].id, v: dz }); }
      } }
    done('crossing_heights_match', w, bad, n, `${n} plan crossings, all dz < 0.05 m or >= 5.3 m`); }

  // onOpening(x, y, z): the point lies on a tunnel portal's opening edge (in the portal plane, on the lining's inner arch): the headwall soffit, which by definition has air below it
  const onOpening = (x, y, z) => { const vs = P.tunnelVolumes ? P.tunnelVolumes() : [];
    for (const v of vs) for (const [p, sg] of [[v.st[0], 1], [v.st[v.st.length - 1], -1]]) { const nx = Math.cos(p.h) * sg, ny = Math.sin(p.h) * sg, dx = x - p.x, dy = y - p.y;
      if (Math.abs(dx * nx + dy * ny) > 0.03) continue; const la = Math.abs(-dx * Math.sin(p.h) + dy * Math.cos(p.h)), rel = z - p.z - (v.road.id % 64) * 0.0025;   // 3 cm: the face columns are keyed at 5 cm   // roadmesh lifts each road by zo = (id % 64) * 2.5 mm
      if (la <= v.prof.W + 0.03 && rel >= v.prof.Hs - 0.03 && Math.abs(rel - P.tunnelRoof(v.prof, Math.min(la, v.prof.W))) < 0.03) return true; }
    return false; };
  // no_floating_faces (wall / portal / abutment meshes):
  //  top: no vertex outside the road footprint (+0.9 m) higher than max(groundAt, road z) + 0.6 m (parapet allowance)
  //  bottom: the lowest vertex of every plan column (same x,y) is at most 0.3 m above groundAt (the face meets the ground)
  { let n = 0, bad = 0, w = null, nBot = 0, badBot = 0, wBot = null;
    for (const m of meshes) { const kd = m.userData.kind; if (!(kd === 'wall' || kd === 'portal' || kd === 'abutment')) continue; if (!m.userData.road) continue;
      const pos = m.geometry.attributes.position.array, cols = new Map();
      for (let v = 0; v < pos.length; v += 3) {
        const x = pos[v], y = -pos[v + 2], z = pos[v + 1], key = Math.round(x * 20) * 1000003 + Math.round(y * 20), c = cols.get(key);
        if (!c) cols.set(key, { x, y, z }); else if (z < c.z) c.z = z;
        const best = nearStation(x, y); if (!best) continue; const [r, i] = best, s = r.samples[i], hw = RT[r.type].total / 2;
        const lat = (x - s.x) * -Math.sin(s.h) + (y - s.y) * Math.cos(s.h);
        if (Math.abs(lat) <= hw + ((kd === 'wall' || kd === 'portal') ? 1.6 : 0.9) || isTu(s) || isBr(s)) continue; n++;
        const ex = z - (Math.max(groundAt(x, y), s.z) + 0.6);
        if (ex > 0.02) { bad++; w = track(w, { x, y, road: r.id, what: kd + ' vertex above surface by', v: ex }); }
      }
      for (const c of cols.values()) { if (kd === 'portal' && onOpening(c.x, c.y, c.z)) continue; nBot++; const ex = c.z - groundAt(c.x, c.y) - 0.3; /* tunnel opening edges are the soffit of the headwall, not a foundation */ if (ex > 0.001) { badBot++; wBot = track(wBot, { x: c.x, y: c.y, road: m.userData.road.id, what: kd + ' bottom floats above ground by', v: ex + 0.3 }); } }
    }
    done('no_floating_faces', w, bad, n, `${n} outer vertices within surface + 0.6 m`);
    const sel = results[results.length - 1];
    if (badBot) { sel.pass = false; sel.msg = (bad ? sel.msg + ' ; ' : '') + `${badBot}/${nBot} columns: worst ${wBot.what} at ${fmt(wBot.x, wBot.y)} road#${wBot.road} ${wBot.v.toFixed(3)}m`; }
    else sel.msg += `; ${nBot} face columns meet ground within 0.3 m`; }
  // ground_hidden_by_walls: along every wall run, the graded ground must not poke through the wall. Samples at lateral offsets from just
  // inside the road face (-0.3) to just outside the outer face (hi+0.3), every 0.1 m: (a) ground between the faces (lo..hi) <= wall top - 0.3;
  // (b) ground just outside either face <= wall top; (c) outside the outer face the ground is natural again (within 0.25 m of terrainH): no ramp left outside.
  { let n = 0, bad = 0, w = null; const cat = {}; const nearOther = (r0, x, y) => roads.some(o => o !== r0 && o.samples.some(q => Math.abs(q.x - x) < 45 && Math.abs(q.y - y) < 45 && Math.hypot(q.x - x, q.y - y) < RT[o.type].total / 2 + 12));   // another road's formation / slope apron reaches here
    const hwOf = r => RT[r.type].total / 2;
    for (const r of roads) for (const run of wallRuns(r)) {
      const sg = run.side === 'L' ? 1 : -1, lo = run.lo != null ? run.lo : 0, hi = run.hi != null ? run.hi : 1.0, hw = hwOf(r);
      const cum = [0]; for (let i = 1; i < run.pts.length; i++) cum.push(cum[i - 1] + Math.hypot(run.pts[i].x - run.pts[i - 1].x, run.pts[i].y - run.pts[i - 1].y));
      for (let pi = 0; pi < run.pts.length; pi++) {
        const p = run.pts[pi], endZone = cum[pi] < 12 || cum[cum.length - 1] - cum[pi] < 12;   // the slope run beyond a wall end aprons round it; ramp check skipped there
        const top = p.top != null ? p.top : p.zTop, ux = -Math.sin(p.h) * sg, uy = Math.cos(p.h) * sg;
        for (let e = lo - 0.3; e <= hi + 0.3 + 1e-9; e += 0.1) {
          const x = p.x + ux * e, y = p.y + uy * e, g = groundAt(x, y); n++;
          const inside = e >= lo - 1e-9 && e <= hi + 1e-9, lim = inside ? top - 0.3 : top;
          if (g > lim + 0.02) { bad++; { const k = 'r' + r.id + (run.cut ? 'c' : 'f') + (inside ? 'in' : 'out'); cat[k] = (cat[k] || 0) + 1; } w = track(w, { x, y, road: r.id, what: run.side + (run.cut ? ' cut' : ' fill') + ' wall ground above ' + (inside ? 'top-0.3' : 'top') + ' at e=' + e.toFixed(1) + ' by', v: g - lim }); }
          else if (!endZone && e > hi + 0.1 && !nearOther(r, x, y) && Math.abs(g - terrainH(x, y)) > 0.25) { bad++; { const k = 'r' + r.id + (run.cut ? 'c' : 'f') + 'ramp'; cat[k] = (cat[k] || 0) + 1; } w = track(w, { x, y, road: r.id, what: run.side + ' wall ramp outside outer face, off natural at e=' + e.toFixed(1) + ' by', v: Math.abs(g - terrainH(x, y)) }); }
        }
      } }
    done('ground_hidden_by_walls', w, bad, n, n ? `${n} ground samples across wall faces, all hidden or natural` : 'WARNING no wall runs'); if (!n) results[results.length - 1].pass = false; if (bad) results[results.length - 1].msg += ' [' + Object.entries(cat).map(([k, v]) => k + ':' + v).join(' ') + ']'; }

  // no_bare_steps: every neighbouring-texel step > 1.0 m in GH, away from bridge/tunnel zones, must lie inside a wall solid
  // (wallRuns point: lateral [lo,hi] from the wall line +-0.6 m, within 2.5 m along). Anything else is a bare near-vertical ground face.
  { const GR = P.GRID_RES, GNN = Math.round(2048 / GR) + 1, GHH = P.GH, W = new Map(), WC = 4; let n = 0, bad = 0, w = null;
    for (const r of roads) for (const run of wallRuns(r)) { const sg = run.side === 'L' ? 1 : -1, lo = run.lo != null ? run.lo : 0, hi = run.hi != null ? run.hi : 1.0;
      for (const p of run.pts) { const key = Math.floor(p.x / WC) * 100003 + Math.floor(p.y / WC); let l = W.get(key); if (!l) W.set(key, l = []); l.push({ p, sg, lo, hi }); } }
    const inWall = (x, y) => { const cx = Math.floor(x / WC), cy = Math.floor(y / WC);
      for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) { const l = W.get((cx + a) * 100003 + cy + b); if (!l) continue;
        for (const q of l) { const dx = x - q.p.x, dy = y - q.p.y; if (dx * dx + dy * dy > 3.5 * 3.5) continue;
          const e = (dx * -Math.sin(q.p.h) + dy * Math.cos(q.p.h)) * q.sg, al = dx * Math.cos(q.p.h) + dy * Math.sin(q.p.h);
          if (e >= q.lo - 0.8 && e <= q.hi + 0.8 && Math.abs(al) <= 2.5) return true; } }
      return false; };
    const stuct = (x, y) => { const cx = Math.floor(x / SC), cy = Math.floor(y / SC), m = 3;   // within 8 m of a bridge/tunnel station
      for (let a = -m; a <= m; a++) for (let b = -m; b <= m; b++) { const l = sh.get((cx + a) * 100003 + cy + b); if (!l) continue; for (const [r, i] of l) { const s = r.samples[i]; if ((isBr(s) || isTu(s)) && (s.x - x) ** 2 + (s.y - y) ** 2 < (RT[r.type].total / 2 + 12) ** 2) return true; } }
      return false; };
    const GW = Math.min(GNN, P.GN || GNN);
    for (let j = 0; j < GW; j++) for (let i = 0; i < GW; i++) { const g = GHH[j * GW + i];
      for (const [di, dj] of [[1, 0], [0, 1]]) { if (i + di >= GW || j + dj >= GW) continue; const st = Math.abs(GHH[(j + dj) * GW + i + di] - g); if (st <= 1.0) continue; n++;
        const x = (i + di / 2) * GR, y = (j + dj / 2) * GR;
        if (inWall(x, y) || stuct(x, y)) continue; bad++; w = track(w, { x, y, road: 0, what: 'bare ground step', v: st }); } }
    done('no_bare_steps', w, bad, n, `${n} texel steps > 1 m, all inside wall solids or bridge/tunnel zones`); }
  // no_footway_on_crossing: at-grade crossings: no footway / kerb / marking triangle of one road may lie more than 0.75 m inside another road's carriageway (within 1 m height).
  // (A footway wedge lying on the crossing road's asphalt was the cross_slope / tee_slope sliver.)
  { let n = 0, bad = 0, w = null;
    for (const m of meshes) { const ly = m.userData.layer; if (ly !== 'pave' && ly !== 'kerb' && ly !== 'mark') continue; const r = m.userData.road; if (!r) continue;
      const pos = m.geometry.attributes.position.array, idx = m.geometry.index.array;
      for (let t = 0; t < idx.length; t += 3) { const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
        const x = (pos[a] + pos[b] + pos[c]) / 3, y = -(pos[a + 2] + pos[b + 2] + pos[c + 2]) / 3, z = (pos[a + 1] + pos[b + 1] + pos[c + 1]) / 3; n++;
        for (const r2 of roads) { if (r2 === r) continue; const lim = RT[r2.type].carriageway / 2 - 0.75, S2 = r2.samples; let hit = false;
          for (let q = 0; q + 1 < S2.length && !hit; q++) { const p0 = S2[q], p1 = S2[q + 1];
            if (x < Math.min(p0.x, p1.x) - lim || x > Math.max(p0.x, p1.x) + lim || y < Math.min(p0.y, p1.y) - lim || y > Math.max(p0.y, p1.y) + lim) continue;
            const dx = p1.x - p0.x, dy = p1.y - p0.y, l2 = dx * dx + dy * dy, u = l2 > 1e-12 ? Math.min(1, Math.max(0, ((x - p0.x) * dx + (y - p0.y) * dy) / l2)) : 0, d = Math.hypot(x - p0.x - dx * u, y - p0.y - dy * u), dz = z - (p0.z + (p1.z - p0.z) * u);
            if (d < lim && Math.abs(dz) < 1.0) hit = true; }
          if (hit) { bad++; w = track(w, { x, y, road: r.id, what: ly + ' triangle on road#' + r2.id + ' carriageway, depth', v: 0 }); break; } } } }
    done('no_footway_on_crossing', w, bad, n, `${n} footway/kerb/marking triangles, none lie on another road's carriageway`); }
  // ===== tunnels: bore clear (terrain discard) / step at the portal plane / lining closed =====
  // Volumes come from the same data the terrain shader reads (earthworks.js packTunnels); P.tunnelDiscard is the JS mirror of the shader's inBore() on that very table.
  const vols = P.tunnelVolumes ? P.tunnelVolumes() : [], TUN = P.TUN;
  const planesOf = v => { const S = v.st, a = S[0], b = S[S.length - 1];   // portal planes: station, unit normal into the tunnel, unit lateral
    return [{ p: a, nx: Math.cos(a.h), ny: Math.sin(a.h) }, { p: b, nx: -Math.cos(b.h), ny: -Math.sin(b.h) }].map(q => ({ ...q, lx: -Math.sin(q.p.h), ly: Math.cos(q.p.h) })); };
  const tFrame = (q, al, la) => [q.p.x + q.nx * al + q.lx * la, q.p.y + q.ny * al + q.ly * la];
  if (!vols.length) results.push({ name: 'tunnel_bore_clear', pass: false, msg: 'no tunnels in the scene' }, { name: 'tunnel_step_at_portal', pass: false, msg: 'no tunnels' }, { name: 'tunnel_lining_closed', pass: false, msg: 'no tunnels' });
  else {
    // ---- tunnel_bore_clear ----
    { let n = 0, bad = 0, w = null, nGround = 0, nDisc = 0; const off = TUN.lining + TUN.margin;
      for (const v of vols) { const pr = v.prof, W = pr.W, S = v.st;
        // (a) the discard volume is complete: points inside the inner clear profile (every station, between stations, curved bends) are discarded; points just outside the outer profile / in front of the plane are not
        for (let k = 0; k + 1 < S.length; k++) for (const f of [0.03, 0.25, 0.5, 0.75, 0.97]) { const a = S[k], b = S[k + 1], x = a.x + (b.x - a.x) * f, y = a.y + (b.y - a.y) * f, z = a.z + (b.z - a.z) * f, h = Math.atan2(b.y - a.y, b.x - a.x), lx = -Math.sin(h), ly = Math.cos(h);
          for (const q of [0, W * 0.5, W - 0.05, -(W - 0.05)]) for (const rel of [0.05, P.tunnelRoof(pr, Math.abs(q)) - 0.05]) { n++; if (!P.tunnelDiscard(x + lx * q, y + ly * q, z + rel)) { bad++; w = track(w, { x, y, road: v.road.id, what: 'point inside the bore not discarded (q ' + q.toFixed(1) + ', rel ' + rel.toFixed(1) + ') ', v: 0 }); } }
          for (const [q, rel, what] of [[0, P.tunnelRoof(pr, 0, off) + 0.3, 'above the crown'], [W + off + 0.3, 2, 'beside the bore']]) { n++; if (P.tunnelDiscard(x + lx * q, y + ly * q, z + rel)) { bad++; w = track(w, { x, y, road: v.road.id, what: 'discard reaches ' + what, v: 0 }); } } }
        // (b) ground samples: every terrain point whose height lies inside the bore volume is discarded, none is left standing in the clear profile or lining
        for (const q of planesOf(v)) {
          for (let al = -3; al <= 3; al += 0.25) for (let la = -(W + off + 1); la <= W + off + 1; la += 0.25) { const [x, y] = tFrame(q, al, la), g = P.groundAt(x, y), rel = g - q.p.z, roofO = P.tunnelRoof(pr, Math.min(Math.abs(la), W + off), off);
            const inside = al > 0.1 && Math.abs(la) <= W + off - 0.1 && rel >= -TUN.floorDepth + 0.3 && rel <= roofO - 0.3; n++;
            if (inside) { nGround++; if (P.tunnelDiscard(x, y, g)) nDisc++; else { bad++; w = track(w, { x, y, road: v.road.id, what: 'ground inside the bore volume left undiscarded, al ' + al.toFixed(2), v: 0 }); } }
            else if (al < -0.05 && Math.abs(la) <= W && P.tunnelDiscard(x, y, g)) { bad++; w = track(w, { x, y, road: v.road.id, what: 'discard reaches in front of the portal plane, al ' + al.toFixed(2), v: 0 }); } } } }
      // (c) render probe: terrain fragments painted magenta; camera 30 m before each entry portal at eye height, looking along the road into the opening. No magenta inside the opening
      // (the far portal's own opening is masked: terrain legitimately shows through it). Control: with the discard switched off the same view must show many magenta pixels.
      const cam = P.camera, tg = P.controls.target, save = { p: cam.position.clone(), t: tg.clone() }, gl = document.getElementById('view').getContext('webgl2'), THREE = P.THREE;
      const probeOne = (v, off) => { const S = v.road.samples, a = v.a, b = v.b, ia = Math.max(0, a - 15), pr = v.prof, rp = S[ia], rp1 = S[a], pe = S[a + Math.min(8, b - a)];
        P.freeCam([rp.x, rp.y, rp.z + 1.6], [pe.x, pe.y, pe.z + 2.2]); P.tunRender({ probe: true, off }); P.renderNow(); cam.updateMatrixWorld();
        const W2 = gl.drawingBufferWidth, H2 = gl.drawingBufferHeight, px = new Uint8Array(W2 * H2 * 4); gl.readPixels(0, 0, W2, H2, gl.RGBA, gl.UNSIGNED_BYTE, px);
        const outline = (q) => { const pts = []; const prof = [[-pr.W, 0.12], [-pr.W, pr.Hs]]; for (let j = 1; j < TUN.archN; j++) { const l = -pr.W + 2 * pr.W * j / TUN.archN; prof.push([l, P.tunnelRoof(pr, Math.abs(l))]); } prof.push([pr.W, pr.Hs], [pr.W, 0.12]);
          for (const [l, hh] of prof) { const [x, y] = tFrame(q, 0, l), vv = new THREE.Vector3(x, q.p.z + hh, -y).project(cam); pts.push([(vv.x * 0.5 + 0.5) * W2, (vv.y * 0.5 + 0.5) * H2]); } return pts; };
        const inside = (pts, x, y) => { let c = false; for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) if ((pts[i][1] > y) !== (pts[j][1] > y) && x < (pts[j][0] - pts[i][0]) * (y - pts[i][1]) / (pts[j][1] - pts[i][1]) + pts[i][0]) c = !c; return c; };
        const near = outline(planesOf(v)[0]), far = outline(planesOf(v)[1]);
        let xs = near.map(p => p[0]), ys = near.map(p => p[1]); const x0 = Math.max(0, Math.floor(Math.min(...xs))), x1 = Math.min(W2 - 1, Math.ceil(Math.max(...xs))), y0 = Math.max(0, Math.floor(Math.min(...ys))), y1 = Math.min(H2 - 1, Math.ceil(Math.max(...ys)));
        const eroded = (x, y) => inside(near, x, y) && [[3, 0], [-3, 0], [0, 3], [0, -3]].every(d => inside(near, x + d[0], y + d[1])) && !(far.length && [[0, 0], [4, 0], [-4, 0], [0, 4], [0, -4]].some(d => inside(far, x + d[0], y + d[1])));
        let tested = 0, mag = 0; for (let y = y0; y <= y1; y += 2) for (let x = x0; x <= x1; x += 2) { if (!eroded(x, y)) continue; tested++; const i = (y * W2 + x) * 4; if (px[i] > 240 && px[i + 1] < 16 && px[i + 2] > 240) mag++; }
        return { tested, mag }; };
      let probeMsg = []; let ctl = null;
      for (const v of vols) { const r1 = probeOne(v, false); if (r1.tested < 50) { bad++; w = { x: v.st[0].x, y: v.st[0].y, road: v.road.id, what: 'render probe: opening not visible (' + r1.tested + ' px tested)', v: 0 }; } else { n += r1.tested; if (r1.mag) { bad++; w = track(w, { x: v.st[0].x, y: v.st[0].y, road: v.road.id, what: 'terrain-coloured pixels inside the opening: ' + r1.mag + '/' + r1.tested, v: 0 }); } }
        probeMsg.push(r1.tested + 'px'); if (!ctl) ctl = probeOne(v, true); }
      P.tunRender({ probe: false, off: false }); P.endFreeCam(); cam.position.copy(save.p); tg.copy(save.t); cam.lookAt(tg); P.renderNow();
      if (ctl && ctl.mag < 100) { bad++; w = { x: 0, y: 0, road: 0, what: 'probe control failed: with the discard off only ' + ctl.mag + ' magenta pixels', v: 0 }; }
      done('tunnel_bore_clear', w, bad, n, `${vols.length} tunnels: ${nGround} ground samples inside bore volumes all discarded (${nDisc}), volume gap-free along curves, none above the crown / in front of the plane; render probe 0 terrain pixels in the openings (${probeMsg.join(', ')} tested; control with discard off: ${ctl ? ctl.mag : '?'} magenta)`); }
    // ---- tunnel_step_at_portal ----
    { let n = 0, bad = 0, w = null, maxAl = 0; const NAT = TUN.planeC + Math.SQRT2 * P.GRID_RES + 0.05;   // natural by here: graded texels stop planeC past the plane, a triangle spans at most one texel diagonal
      for (const v of vols) { const hw = RT[v.road.type].total / 2;
        for (const q of planesOf(v)) {
          for (let la = -(hw + 1.5); la <= hw + 1.5 + 1e-9; la += 0.25) { for (let al = NAT; al <= TUN.headT + 1e-9; al += 0.25) { const [x, y] = tFrame(q, al, la); n++; const d = Math.abs(P.groundAt(x, y) - terrainH(x, y)); maxAl = Math.max(maxAl, al);
              if (d > 0.3) { bad++; w = track(w, { x, y, road: v.road.id, what: 'ground not back at natural ' + al.toFixed(2) + ' m behind the portal plane, off by', v: d }); } }
            if (Math.abs(la) <= hw) for (let al = -4; al <= 0; al += 0.25) { const [x, y] = tFrame(q, al, la), g = P.groundAt(x, y); n++; const nb = nearStation(x, y), d = g - ((nb ? nb[0].samples[nb[1]].z : q.p.z) - 0.15);   // in front of the plane: formation, no ramp
              if (Math.abs(d) > 0.15 && !(otherRoadNear(x, y, v.road, 6))) { bad++; w = track(w, { x, y, road: v.road.id, what: 'ground off formation in front of the portal plane (al ' + al.toFixed(2) + '), by', v: Math.abs(d) }); } } } } }
      done('tunnel_step_at_portal', w, bad, n, `${vols.length * 2} portal planes: ground in front is at formation, back at natural ground by ${NAT.toFixed(2)} m behind the plane (planeC ${TUN.planeC} + one 0.5 m texel diagonal) and ${TUN.headT} m headwall hides the ramp`); }
    // ---- tunnel_lining_closed ----
    { let n = 0, bad = 0, w = null, nEdge = 0, nTri = 0;
      const key = (x, y, z) => Math.round(x * 1000) + ',' + Math.round(y * 1000) + ',' + Math.round(z * 1000);
      const idx3 = pts => { const m = new Map(); for (const p of pts) { const k = Math.floor(p[0] / 0.01) + ',' + Math.floor(p[1] / 0.01) + ',' + Math.floor(p[2] / 0.01); let l = m.get(k); if (!l) m.set(k, l = []); l.push(p); } return m; };
      const has = (m, p, tol = 2e-3) => { const cx = Math.floor(p[0] / 0.01), cy = Math.floor(p[1] / 0.01), cz = Math.floor(p[2] / 0.01);
        for (let a = -1; a <= 1; a++) for (let b = -1; b <= 1; b++) for (let c = -1; c <= 1; c++) { const l = m.get((cx + a) + ',' + (cy + b) + ',' + (cz + c)); if (l) for (const q of l) if (Math.abs(q[0] - p[0]) < tol && Math.abs(q[1] - p[1]) < tol && Math.abs(q[2] - p[2]) < tol) return true; } return false; };
      const vertsOf = ms => { const out = []; for (const m of ms) { const pos = m.geometry.attributes.position.array; for (let i = 0; i < pos.length; i += 3) out.push([pos[i], pos[i + 1], pos[i + 2]]); } return out; };
      const byRoad = new Map(); for (const v of vols) { if (!byRoad.has(v.road)) byRoad.set(v.road, []); byRoad.get(v.road).push(v); }
      for (const [road, vs] of byRoad) {
        const lin = [], por = [], rest = []; for (const m of meshes) { if (m.userData.road !== road) continue; if (m.userData.kind === 'lining' && !m.userData.layer) lin.push(m); else if (m.userData.kind === 'portal') por.push(m); else if (m.userData.kind !== 'lining' || m.userData.layer === 'road') rest.push(m); }
        if (!lin.length) { bad++; w = { x: vs[0].st[0].x, y: vs[0].st[0].y, road: road.id, what: 'tunnel has no lining mesh', v: 0 }; continue; }
        const linI = idx3(vertsOf(lin)), porI = idx3(vertsOf(por)), restI = idx3(vertsOf(rest)), pls = vs.flatMap(planesOf), S = road.samples, Hs = vs[0].prof.Hs;
        // facing: every lining triangle faces the tunnel air (geometric normal from the winding AND the stored vertex normal), so nothing is a backface seen from inside
        for (const m of lin) { const pos = m.geometry.attributes.position.array, nor = m.geometry.attributes.normal.array, idx = m.geometry.index.array;
          for (let t = 0; t < idx.length; t += 3) { const a = idx[t] * 3, b = idx[t + 1] * 3, c = idx[t + 2] * 3;
            const ux = pos[b] - pos[a], uy = pos[b + 1] - pos[a + 1], uz = pos[b + 2] - pos[a + 2], vx = pos[c] - pos[a], vy = pos[c + 1] - pos[a + 1], vz = pos[c + 2] - pos[a + 2];
            let gx = uy * vz - uz * vy, gy = uz * vx - ux * vz, gz = ux * vy - uy * vx; const gl = Math.hypot(gx, gy, gz) || 1; gx /= gl; gy /= gl; gz /= gl;
            const cx = (pos[a] + pos[b] + pos[c]) / 3, cy = (pos[a + 1] + pos[b + 1] + pos[c + 1]) / 3, cz = (pos[a + 2] + pos[b + 2] + pos[c + 2]) / 3;
            let bi = 0, bd = 1e18; S.forEach((s, i) => { if (!isTu(s)) return; const d = (s.x - cx) ** 2 + (s.y + cz) ** 2; if (d < bd) { bd = d; bi = i; } });
            const ax = S[bi].x, ay = S[bi].z + Hs * 0.5, az = -S[bi].y; n++; nTri++;
            if (gx * (ax - cx) + gy * (ay - cy) + gz * (az - cz) <= 0 || gx * nor[a] + gy * nor[a + 1] + gz * nor[a + 2] < 0.99) { bad++; w = track(w, { x: cx, y: -cz, road: road.id, what: 'lining triangle faces away from the bore', v: 0 }); } } }
        // closure: boundary edges (used by one triangle) lie only on a portal plane or on the road edge line, and every vertex there matches the neighbouring surface exactly
        const ec = new Map(), addE = (p0, p1) => { const k0 = key(...p0), k1 = key(...p1), kk = k0 < k1 ? k0 + '|' + k1 : k1 + '|' + k0, e = ec.get(kk); if (e) e.c++; else ec.set(kk, { c: 1, p0, p1 }); };
        for (const m of lin) { const pos = m.geometry.attributes.position.array, idx = m.geometry.index.array, P3 = i => [pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]];
          for (let t = 0; t < idx.length; t += 3) for (const [i, j] of [[0, 1], [1, 2], [2, 0]]) addE(P3(idx[t + i]), P3(idx[t + j])); }
        for (const e of ec.values()) { if (e.c !== 1) continue; nEdge++;
          const pl = p => pls.find(q => Math.abs((p[0] - q.p.x) * q.nx + (-p[2] - q.p.y) * q.ny) < 2e-3);
          if (pl(e.p0) && pl(e.p1)) { for (const p of [e.p0, e.p1]) { n++; const q = pl(p), rel = p[1] - q.p.z - (road.id % 64) * 0.0025, ok = rel >= Hs - 2e-3 ? has(porI, p) : (has(porI, p) || has(restI, p)); if (!ok) { bad++; w = track(w, { x: p[0], y: -p[2], road: road.id, what: 'lining ring vertex has no matching headwall / approach vertex (gap)', v: 0 }); } } continue; }
          let ok = true; for (const p of [e.p0, e.p1]) { n++; if (!has(restI, p)) ok = false; }
          if (!ok) { bad++; w = track(w, { x: e.p0[0], y: -e.p0[2], road: road.id, what: 'open lining edge (neither on a portal plane nor on the road surface)', v: 0 }); } }
        // and the other way round: every headwall vertex on the opening (in the plane, from the springline over the arch) is a lining vertex
        for (const q of pls) for (const m of por) { const pos = m.geometry.attributes.position.array, pr = vs[0].prof; for (let i = 0; i < pos.length; i += 3) { const dx = pos[i] - q.p.x, dy = -pos[i + 2] - q.p.y, al = dx * q.nx + dy * q.ny, la = dx * q.lx + dy * q.ly, rel = pos[i + 1] - q.p.z - (road.id % 64) * 0.0025;
            if (Math.abs(al) < 2e-3 && Math.abs(la) <= pr.W + 2e-3 && rel >= pr.Hs - 2e-3 && rel <= pr.Hc + 2e-3) { n++; if (!has(linI, [pos[i], pos[i + 1], pos[i + 2]])) { bad++; w = track(w, { x: pos[i], y: -pos[i + 2], road: road.id, what: 'headwall opening vertex has no lining vertex (sliver)', v: 0 }); } } } } }
      done('tunnel_lining_closed', w, bad, n, `${vols.length} tunnels: ${nTri} lining triangles all face the bore (winding + normals); ${nEdge} boundary edges all on a portal plane (vertex-matched to the headwall opening / approach) or on the road edge line`); }
  }
  return results;
}
