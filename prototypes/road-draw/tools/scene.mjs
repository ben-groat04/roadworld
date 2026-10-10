// Canonical test scene + fixed camera views for road-draw. Everyone checks the same hard cases.
//
//   node tools/scene.mjs <outDir> [viewName ...]      (default: all views)
//   INFO=1 node tools/scene.mjs <outDir> overview     (also prints per-feature structure stats)
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
// 5. tunnel: residential road heading +y into the escarpment (ground 50 -> 85 m); z ramps 40 -> 56 so the road sinks > wallCap below ground.
//    Portal is found automatically (first tunnel sample); views are placed relative to it.  [x, y, z]
const TUNNEL = [[300, 1230, 40], [300, 1400, 46], [300, 1580, 56]];
const TUNNEL_FRONT_DIST = 40;   // m in front of the portal, along the road
// 6. dual_curve: A-road dual carriageway, ONE stroke, big arc (minR 720). Points on a circle: centre, radius, start/end angle (deg), count.
const DUAL = { cc: [1000, -300], R: 1000, a0: 25, a1: 60, n: 9 };
// 7. motorway on embankment: straight across a shallow dip (ground ~50 m at x=1050, 65-75 m on the sides), deck held ~60 m. [x, y, z]
const MOTORWAY = [[800, 1000, 62], [1050, 1000, 62], [1300, 1000, 62]];

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
  motorway: () => ({ target: [1050, 1000], dist: 90, elev: 18, look: 35 }),
  overview: () => ({ target: [850, 850], dist: 2700, elev: 50, look: 100 }),
};
// ======================================================================================================================

const outDir = resolve(process.argv[2] || '.');
const wanted = process.argv.slice(3);
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
