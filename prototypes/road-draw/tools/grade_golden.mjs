// Golden data for the sim_core grading port (sim_core/src/grading.cpp, tests/test_grading.cpp).
// Runs the prototype's own src/earthworks.js headlessly (node:vm, no browser, no THREE) on a handful of deterministic
// cases and writes tests/data/grading/<case>.json: the sampled roads (inputs), the classification / wall weights / reach /
// portal planes, graded tiles (heights quantised to 0.1 mm), wall runs, the packed tunnel table and inBore probe answers.
// Usage: node prototypes/road-draw/tools/grade_golden.mjs [outDir]       (default outDir: tests/data/grading)
// Nothing under src/ is modified; this file only reads it.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const src = f => readFileSync(new URL(`../src/${f}`, import.meta.url), 'utf8');
const outDir = process.argv[2] ? process.argv[2] : fileURLToPath(new URL('../../../tests/data/grading/', import.meta.url));
mkdirSync(outDir, { recursive: true });

// ---- headless environment: config constants + geom + stubs for what terrain.js provides + earthworks.js ----
const ctx = vm.createContext({ Math, Float32Array, Uint8Array, Array, Object, JSON, console, __terrain: (x, y) => 0 });
const run = code => vm.runInContext(code, ctx);
run(src('config.js').split('\n').filter(l => !/^import /.test(l)).join('\n'));
run(src('geom.js'));
run(`const hBase = (x, y) => __terrain(x, y), hBaseFine = hBase, terrainH = hBase, CELL = 4, GRID_RES = 0.5, clamp = (v, a, b) => Math.min(b, Math.max(a, v));`);
run(src('earthworks.js'));

// ---- analytic natural ground, evaluated identically in C++ (tests/test_grading.cpp eval_terrain) ----
// terms: plane {c0,cx,cy}; gauss {amp,cx,cy,sx,sy}; slopeEnv {slope,yc,cx,sig}; sin {amp,fx,fy,ph}. Local coords = world - off.
function terrainFn(tr) {
  const [ox, oy] = tr.off;
  return (X, Y) => {
    const x = X - ox, y = Y - oy; let z = 0;
    for (const t of tr.terms) {
      if (t.k === 'plane') z += t.c0 + t.cx * x + t.cy * y;
      else if (t.k === 'gauss') z += t.amp * Math.exp(-((x - t.cx) * (x - t.cx) / (2 * t.sx * t.sx) + (y - t.cy) * (y - t.cy) / (2 * t.sy * t.sy)));
      else if (t.k === 'slopeEnv') z += t.slope * (y - t.yc) * Math.exp(-((x - t.cx) * (x - t.cx)) / (2 * t.sig * t.sig));
      else if (t.k === 'sin') z += t.amp * Math.sin(t.fx * x + t.fy * y + t.ph);
    }
    return z;
  };
}
const ripple = { k: 'sin', amp: 0.25, fx: 0.11, fy: 0.07, ph: 0.4 };
const NOISE_OFF = [0, 0];

// ---- cases: roads (type, control points x,y,z in local coords) and terrain ----
const CASES = [
  { name: 'straight_crossfall', off: NOISE_OFF, terms: [{ k: 'plane', c0: 20, cx: 0, cy: 0.25 }, ripple], roads: [{ type: 2, pts: [[0, 0, 20], [240, 0, 20]] }] },
  { name: 'cutting', off: NOISE_OFF, terms: [{ k: 'plane', c0: 20, cx: 0, cy: 0.03 }, { k: 'gauss', amp: 9, cx: 130, cy: 0, sx: 40, sy: 60 }, ripple], roads: [{ type: 2, pts: [[0, 0, 20], [260, 0, 20]] }] },
  { name: 'embankment', off: NOISE_OFF, terms: [{ k: 'plane', c0: 20, cx: 0, cy: -0.04 }, { k: 'gauss', amp: -7, cx: 130, cy: 0, sx: 45, sy: 70 }, ripple], roads: [{ type: 2, pts: [[0, 0, 20], [260, 0, 20]] }] },
  { name: 'tight_curve_high_fill', off: NOISE_OFF, terms: [{ k: 'plane', c0: 18, cx: 0, cy: 0.05 }, { k: 'gauss', amp: -9.5, cx: 85, cy: 25, sx: 40, sy: 40 }, ripple], roads: [{ type: 1, pts: [[0, 0, 18], [60, 0, 18], [100, 30, 18], [100, 90, 18]] }] },
  { name: 'wall_steep_side', off: NOISE_OFF, terms: [{ k: 'plane', c0: 30, cx: 0, cy: 0 }, { k: 'slopeEnv', slope: 0.7, yc: 0, cx: 130, sig: 50 }, ripple], roads: [{ type: 2, pts: [[0, 0, 30], [260, 0, 30]] }] },
  { name: 'bridge_valley', off: NOISE_OFF, terms: [{ k: 'plane', c0: 30, cx: 0, cy: 0.02 }, { k: 'gauss', amp: -22, cx: 140, cy: 0, sx: 28, sy: 1e4 }, ripple], roads: [{ type: 3, pts: [[0, 0, 30], [280, 0, 30]] }] },
  { name: 'tunnel_curved_grade', off: [15000, -9000], terms: [{ k: 'plane', c0: 12, cx: 0.01, cy: 0 }, { k: 'gauss', amp: 45, cx: 150, cy: 50, sx: 45, sy: 45 }, ripple], roads: [{ type: 2, pts: [[0, 0, 10], [90, 20, 13], [180, 70, 17], [240, 150, 20]] }] },
  { name: 'dual_pair', off: NOISE_OFF, terms: [{ k: 'plane', c0: 20, cx: 0, cy: 0.15 }, { k: 'gauss', amp: 6, cx: 100, cy: 0, sx: 40, sy: 50 }, ripple], roads: [{ type: 3, pts: [[0, 0, 20], [200, 0, 20]] }, { type: 3, pts: [[200, 13.3, 20], [0, 13.3, 20]] }] },
  { name: 'crossing_and_ends', off: NOISE_OFF, terms: [{ k: 'plane', c0: 20, cx: 0, cy: 0 }, { k: 'gauss', amp: 5, cx: 60, cy: 30, sx: 30, sy: 30 }, ripple], roads: [{ type: 2, pts: [[0, 0, 20], [160, 0, 20]] }, { type: 1, pts: [[100, -70, 20], [100, 70, 20]] }] },
];

const q4 = v => Math.round(v * 1e4);
const r4 = v => Math.round(v * 1e4) / 1e4;

function buildCase(c) {
  const [ox, oy] = c.off;
  const terrain = { off: c.off, terms: c.terms };
  ctx.__terrain = terrainFn(terrain);
  run('roads.length = 0;');
  ctx.__roadsSpec = c.roads.map(r => ({ type: r.type, pts: r.pts.map(p => ({ x: p[0] + ox, y: p[1] + oy, z: p[2] })) }));
  const ok = run(`__roadsSpec.every(sp => { const r = { type: sp.type, pts: sp.pts, hs: null, he: null, ver: 0, id: roads.length + 1 }; roads.push(r); return rebuild(r); })`);
  if (!ok) throw new Error('rebuild failed in ' + c.name);
  const rd = run('roadsData()');   // markMedians, wallWeights, slopeReach, decimation, portal planes
  const rsJs = run('roads');
  const params = { formDrop: run('FORM_DROP'), batter: run('T.batter'), wallThick: run('WALL_THICK'), verge: run('VERGE'), planeC: run('PLANE_OVER'), roundK: run('ROUND_DAYLIGHT / (2 * T.batter)'), roundHinge: run('ROUND_HINGE') };
  const out = {
    name: c.name, version: 1, quant: 1e-4, terrain,
    params: { ...params, wallStart: run('T.wallStart'), wallCap: run('T.wallCap'), wallSlack: run('WALL_SLACK'), wingMax: run('WING_MAX'), wingMinH: run('WING_MIN_H'), roundDaylight: run('ROUND_DAYLIGHT'), roundReach: run('ROUND_REACH'), tunnel: { lining: run('TUN.lining'), margin: run('TUN.margin'), floorDepth: run('TUN.floorDepth'), riseK: run('TUN.riseK'), clearHeight: run('TUNNEL_H'), headT: run('TUN.headT'), chunk: run('TUN.chunk') } },
    roads: [], tiles: [],
  };
  rsJs.forEach((r, ri) => {
    const ty = run(`ROAD_TYPES[${r.type}]`), S = r.samples;
    const col = f => S.map(f);
    out.roads.push({
      type_id: ty.id, hw: ty.total / 2, dual: !!ty.dual, gap: ty.gap || 0,
      samples: S.map(s => [s.s, s.x, s.y, s.z, s.h]),
      expect: {
        br: col(s => +!!s.br), tu: col(s => +!!s.tu), portal: col(s => +!!s.portal), wL: col(s => s.wL), wR: col(s => s.wR), wL0: col(s => s.wL0), wR0: col(s => s.wR0),
        wwL: col(s => s.wwL), wwR: col(s => s.wwR), mL: col(s => +!!s.mL), mR: col(s => +!!s.mR),
        portals: r.portals, reach: rd[ri].reach, bbox: [rd[ri].bbox.x0, rd[ri].bbox.y0, rd[ri].bbox.x1, rd[ri].bbox.y1],
        ndec: rd[ri].samples.length, planes: rd[ri].planes.map(p => [p.k0, p.k1, p.x, p.y, p.nx, p.ny]),
      },
    });
  });
  // ---- tiles: centred on places where something changes (structure / wall flags, ends), up to 5, 64 x 64 texels of 0.5 m ----
  const picks = [];
  const farFromPicked = (x, y) => picks.every(p => Math.hypot(p[0] - x, p[1] - y) > 26);
  rsJs.forEach(r => { const S = r.samples;
    for (let i = 0; i < S.length; i++) { const a = S[i], b = S[i - 1];
      const ch = i === 0 || i === S.length - 1 || a.br !== b.br || a.tu !== b.tu || a.wL !== b.wL || a.wR !== b.wR;
      if (ch && picks.length < 5 && farFromPicked(a.x, a.y)) picks.push([a.x, a.y]); } });
  rsJs.forEach(r => { const m = r.samples[r.samples.length >> 1]; if (picks.length < 5 && farFromPicked(m.x, m.y)) picks.push([m.x, m.y]); });
  const gp = run('({ formDrop: FORM_DROP, batter: T.batter, wallThick: WALL_THICK, verge: VERGE, planeC: PLANE_OVER, roundK: ROUND_DAYLIGHT / (2 * T.batter), roundHinge: ROUND_HINGE })');
  for (const [cx, cy] of picks) {
    const x0 = Math.round(cx / 0.5) * 0.5 - 16, y0 = Math.round(cy / 0.5) * 0.5 - 16, nx = 64, ny = 64, res = 0.5, o = new Float32Array(nx * ny);
    ctx.__gp = gp; ctx.__rd = rd; ctx.__o = o;
    run(`gradeTile(hBaseFine, __rd, __gp, ${x0}, ${y0}, ${nx}, ${ny}, ${res}, __o)`);
    out.tiles.push({ x0, y0, nx, ny, res, h: Array.from(o, q4) });
  }
  // ---- wall runs (full-resolution samples) ----
  out.walls = rsJs.map((r, ri) => run(`wallRuns(roads[${ri}])`).map(w => ({ side: w.side, cut: w.cut, lo: w.lo, hi: w.hi, pts: w.pts.map(p => [r4(p.x), r4(p.y), r4(p.zTop), r4(p.zBot), r4(p.top), r4(p.h)]) })));
  // ---- tunnel table + inBore probes ----
  const vols = run('tunnelVolumes()');
  const W = run('2 + 2 * (TUN.chunk + 1)'), maxRows = 64, data = new Float32Array(W * maxRows * 4);
  ctx.__data = data; ctx.__vols = vols;
  const rows = run(`packTunnels(__data, ${W}, ${maxRows}, __vols)`);
  const probes = []; let seed = 12345; const rnd = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296;
  ctx.__probe = null;
  for (const v of vols) { const m = v.st.length, off = out.params.tunnel.lining + out.params.tunnel.margin, p = v.prof;
    for (let n = 0; n < 500; n++) {
      const i = Math.floor(rnd() * (m - 1)), f = rnd(), a = v.st[i], b = v.st[i + 1], h = Math.atan2(b.y - a.y, b.x - a.x);
      let x = a.x + (b.x - a.x) * f, y = a.y + (b.y - a.y) * f; const z = a.z + (b.z - a.z) * f;
      if (rnd() < 0.25) { const al = (rnd() * 2 - 1) * 3, sgn = i === 0 ? -1 : (i === m - 2 ? 1 : 0); if (sgn) { x += Math.cos(h) * al * sgn; y += Math.sin(h) * al * sgn; } }
      const lat = (rnd() * 2 - 1) * (p.W + off + 3), rel = -2 + rnd() * (p.Hc + off + 6);
      x += -Math.sin(h) * lat; y += Math.cos(h) * lat;
      const up = z + rel; ctx.__px = x; ctx.__py = y; ctx.__pu = up;
      probes.push([x, y, up, +run(`inBoreData(__data, ${W}, ${rows}, __px, __py, __pu)`)]);
    } }
  out.tunnel = { width: W, rows, data: Array.from(data.subarray(0, rows * W * 4), q4), probes, vols: vols.map(v => ({ road: rsJs.indexOf(v.road), a: v.a, b: v.b, n: v.st.length })) };
  return out;
}

let total = 0;
for (const c of CASES) {
  const g = buildCase(c);
  // compact: one tile / road array per line keeps the files diffable and small
  const text = JSON.stringify(g);
  writeFileSync(`${outDir}/${c.name}.json`, text);
  total += text.length;
  const nWall = g.walls.reduce((a, w) => a + w.reduce((b, r) => b + r.pts.length, 0), 0);
  console.log(`${c.name}: ${g.roads.map(r => r.samples.length).join('+')} samples, ${g.tiles.length} tiles, ${nWall} wall points, ${g.tunnel.rows} tunnel rows, ${(text.length / 1024).toFixed(0)} KB`);
}
console.log(`total ${(total / 1024 / 1024).toFixed(2)} MB -> ${outDir}`);
