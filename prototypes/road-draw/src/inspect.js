// inspect.js: Inspect mode. Loads a scene exported by the C++ sim_core (roads + junction geometry) and renders it with the prototype's own
// road meshes / markings / structures / graded ground. Schema: {version, region, drivingSide, roads:[{id,type,oneWay,samples:[{s,x,y,z,h,structure}]}],
// junctions:[{node,kind,carriageway,pavements,islands,gore,connectors:[{fromEdge,fromLane,toEdge,toLane,turn,enabled,path}],trims:[{edge,atA,dist}]}]}.
// Load: file picker button, drag-and-drop, or ?scene=fixtures/x.json (needs http, e.g. `python -m http.server`; file:// cannot fetch). Key J toggles connector paths.
// Hooks into other files: earthworks.js rebuild() skips r.imported roads; roadsData() appends inspectFootprints() (junction surfaces become graded footprint).
const INSPECT = { group: new THREE.Group(), conn: new THREE.Group(), fp: [], boxes: [], showConn: true, info: null, name: '' };
scene.add(INSPECT.group, INSPECT.conn);

const inspType = t => {
  if (typeof t === 'number') return ROAD_TYPES[t] ? t : 2;
  const s = String(t == null ? '' : t).toLowerCase();
  if (/^\d+$/.test(s) && ROAD_TYPES[+s]) return +s;
  if (/motorway|freeway/.test(s)) return 4;
  if (/dual|a_road|trunk|primary/.test(s)) return 3;
  if (/resid|local|street|service|minor|living/.test(s)) return 1;
  return 2;
};
const inspStruct = v => {   // 0 none, 1 bridge, 2 tunnel
  if (v == null) return 0;
  if (typeof v === 'number') return v === 1 ? 1 : v === 2 ? 2 : 0;
  const s = String(v).toLowerCase();
  return /bridge|viaduct|flyover/.test(s) ? 1 : /tunnel|underpass|culvert/.test(s) ? 2 : 0;
};
const inspRings = a => !a || !a.length ? [] : (typeof a[0][0] === 'number' ? [a] : a).filter(r => r && r.length >= 3);

// Choose where the scene goes: the flattest area (above water) of the base terrain that fits the footprint; z is matched to the mean ground there.
function inspPlace(bb, zRef) {
  const hx = (bb.x1 - bb.x0) / 2, hy = (bb.y1 - bb.y0) / 2, cxs = (bb.x0 + bb.x1) / 2, cys = (bb.y0 + bb.y1) / 2, mg = 60;
  const lo = (h) => Math.min(h + mg, WORLD / 2), hi = (h) => Math.max(WORLD - h - mg, WORLD / 2);
  const xa = lo(hx), xb = hi(hx), ya = lo(hy), yb = hi(hy);
  let best = null;
  for (let cy = ya; cy <= yb + 1e-6; cy += Math.max(24, (yb - ya) / 80))
    for (let cx = xa; cx <= xb + 1e-6; cx += Math.max(24, (xb - xa) / 80)) {
      let mn = 1e9, mx = -1e9, sum = 0, n = 0;
      for (let j = 0; j <= 8; j++) for (let i = 0; i <= 8; i++) {
        const h = hBase(clamp(cx - hx + 2 * hx * i / 8, 0, WORLD), clamp(cy - hy + 2 * hy * j / 8, 0, WORLD));
        if (h < mn) mn = h; if (h > mx) mx = h; sum += h; n++;
      }
      const mean = sum / n, score = (mx - mn) + 0.05 * mean + (mn < T.water + 2 ? 500 : 0);
      if (!best || score < best.score) best = { score, cx, cy, mean };
    }
  return { ox: best.cx - cxs, oy: best.cy - cys, oz: best.mean - zRef, cx: best.cx, cy: best.cy };
}

// One JSON road -> prototype road object (clipped at its trims, resampled to ~2 m, straight-span chain, structure flags from the JSON).
function inspRoad(jr, off, trims) {
  const raw = (jr.samples || []).map(s => ({ x: s.x + off.ox, y: s.y + off.oy, z: s.z + off.oz, st: inspStruct(s.structure) }));
  if (raw.length < 2) return null;
  const cum = [0]; for (let i = 1; i < raw.length; i++) cum.push(cum[i - 1] + Math.hypot(raw[i].x - raw[i - 1].x, raw[i].y - raw[i - 1].y));
  const Lg = cum[cum.length - 1];
  let a0 = 0, a1 = Lg;
  for (const t of trims) if (String(t.edge) === String(jr.id)) { if (t.atA) a0 = Math.max(a0, t.dist); else a1 = Math.min(a1, Lg - t.dist); }
  if (a1 - a0 < 4) return null;
  const n = Math.max(2, Math.ceil((a1 - a0) / 2)), P = [];
  let k = 0;
  for (let q = 0; q <= n; q++) {
    const u = a0 + (a1 - a0) * q / n;
    while (k + 2 < raw.length && cum[k + 1] < u) k++;
    const A = raw[k], B = raw[k + 1], d = cum[k + 1] - cum[k], t = d > 1e-9 ? clamp((u - cum[k]) / d, 0, 1) : 0;
    P.push({ x: A.x + (B.x - A.x) * t, y: A.y + (B.y - A.y) * t, z: A.z + (B.z - A.z) * t, st: t < 0.5 ? A.st : B.st });
  }
  const ty = ROAD_TYPES[inspType(jr.type)], tyId = inspType(jr.type), m = P.length, S = [], spans = [];
  let acc = 0;
  for (let i = 0; i < m; i++) {
    if (i) { const d = Math.hypot(P[i].x - P[i - 1].x, P[i].y - P[i - 1].y); acc += d; spans.push({ x0: P[i - 1].x, y0: P[i - 1].y, h0: Math.atan2(P[i].y - P[i - 1].y, P[i].x - P[i - 1].x), k0: 0, dk: 0, L: d }); }
    S.push(acc);
  }
  const hd = i => { const a = P[Math.max(0, i - 1)], b = P[Math.min(m - 1, i + 1)]; return Math.atan2(b.y - a.y, b.x - a.x); };
  const samples = P.map((p, i) => {
    const h = hd(i), a = Math.max(0, i - 1), b = Math.min(m - 1, i + 1), ds = S[b] - S[a] || 1;
    return { s: S[i], x: p.x, y: p.y, z: p.z, h, k: G.wrap(hd(b) - hd(a)) / ds, g: (P[b].z - P[a].z) / ds, span: Math.min(i, spans.length - 1), t: terrainH(p.x, p.y), _st: p.st };
  });
  const vp = []; for (let i = 0; i < m; i += 12) vp.push({ s: S[i], z: P[i].z, L: 0 }); if (vp[vp.length - 1].s < acc) vp.push({ s: acc, z: P[m - 1].z, L: 0 });
  const r = { id: nextId++, jid: jr.id, type: tyId, oneWay: !!jr.oneWay, pts: vp.map(b => { const s = samples.reduce((q, c) => Math.abs(c.s - b.s) < Math.abs(q.s - b.s) ? c : q); return { x: s.x, y: s.y, z: b.z }; }),
    hs: null, he: null, attach: [], imported: true, chain: { spans, g2: true, kink: 0, L: acc }, S, vp, samples, ver: 1, struct: { bridge: 0, tunnel: 0 } };
  classify(r, ty);
  // Explicit structure from the JSON wins over the fill/cut cap rule used by classify().
  const hw = ty.total / 2;
  r.portals = [];
  for (let i = 0; i < m; i++) {
    const s = samples[i], st = s._st, was = s.br || s.tu;
    delete s._st;
    s.br = s.bridge = st === 1; s.tu = s.tunnel = st === 2;
    if (st) s.wL = s.wR = s.wL0 = s.wR0 = 0;
    else if (was) { const a = needsWall(s, hw, 1), b = needsWall(s, hw, -1); s.wL = s.wL0 = a; s.wR = s.wR0 = b; }
    if (st === 1) r.struct.bridge++; else if (st === 2) r.struct.tunnel++;
  }
  for (let i = 0; i < m; i++) { const s = samples[i]; s.portal = s.tu && (i === 0 || !samples[i - 1].tu || i === m - 1 || !samples[i + 1].tu); if (s.portal) r.portals.push(i); }
  r.bbox = roadBox(r);
  return r;
}

// ---- junction surfaces ----
const inspMat = (o) => new THREE.MeshStandardMaterial({ roughness: 0.92, metalness: 0, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -2, ...o });
const hatchTex = (() => {
  try {
    const cv = document.createElement('canvas'); cv.width = cv.height = 64; const c = cv.getContext('2d');
    c.fillStyle = '#3c3f44'; c.fillRect(0, 0, 64, 64); c.fillStyle = '#e8e6df'; c.fillRect(0, 0, 22, 64);
    const t = new THREE.CanvasTexture(cv); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.SRGBColorSpace; return t;
  } catch (e) { return null; }
})();
const IM = {
  asph: inspMat({ color: 0xffffff, map: MAT.asph.map || null }), pave: inspMat({ color: 0xbdbcb7, polygonOffsetFactor: -2 }), kerb: inspMat({ color: 0x9d9c96 }),
  grass: inspMat({ color: 0x4f7a32, roughness: 1, polygonOffsetFactor: -2 }), paving: inspMat({ color: 0xa9a8a2, polygonOffsetFactor: -2 }),
  gore: inspMat({ color: 0xffffff, map: hatchTex, polygonOffsetFactor: -2 }),
};
if (!IM.asph.map) IM.asph.color.set(0x3c3f44);
const ringArea = r => { let a = 0; for (let i = 0; i < r.length; i++) { const p = r[i], q = r[(i + 1) % r.length]; a += p[0] * q[1] - q[0] * p[1]; } return a / 2; };
function cleanRing(r) {
  const o = [];
  for (const p of r) { const l = o[o.length - 1]; if (!l || Math.hypot(p[0] - l[0], p[1] - l[1]) > 1e-3) o.push(p); }
  while (o.length > 1 && Math.hypot(o[0][0] - o[o.length - 1][0], o[0][1] - o[o.length - 1][1]) <= 1e-3) o.pop();
  if (ringArea(o) < 0) o.reverse();   // CCW seen from above
  return o;
}
// z of a vertex: its own value when finite, else inverse-distance from the nearest road samples.
function inspZ(p) {
  if (Number.isFinite(p[2])) return p[2];
  let sw = 0, sz = 0;
  for (const r of roads) if (r.imported) for (let i = 0; i < r.samples.length; i += 3) { const s = r.samples[i], d = Math.hypot(s.x - p[0], s.y - p[1]); if (d < 40) { const w = 1 / (d + 0.5); sw += w; sz += w * s.z; } }
  return sw ? sz / sw : hBase(p[0], p[1]);
}
// Triangulate rings; returns {ring, tris:[[i,j,k]...]} per ring (each ring an independent polygon).
function inspTriangulate(rings) {
  const out = [];
  for (const raw of rings) {
    const ring = cleanRing(raw);
    if (ring.length < 3) continue;
    const idx = THREE.ShapeUtils.triangulateShape(ring.map(p => new THREE.Vector2(p[0], p[1])), []);
    out.push({ ring, tris: idx });
  }
  return out;
}
function inspPolyGeo(polys, lift, skirt, uv) {
  const P = [], N = [], U = [];
  const v = (p, dz) => { const w = w3(p[0], p[1], p[2] + dz); P.push(w[0], w[1], w[2]); if (uv) { const q = uv(p); U.push(q[0], q[1]); } };
  for (const { ring, tris } of polys) {
    for (const t of tris) {
      let [a, b, c] = t.map(i => ring[i]);
      if ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) < 0) [b, c] = [c, b];   // up-facing
      v(a, lift); v(b, lift); v(c, lift);
    }
    if (skirt) for (let i = 0; i < ring.length; i++) {
      const a = ring[i], b = ring[(i + 1) % ring.length];
      v(a, 0); v(b, 0); v(b, lift); v(a, 0); v(b, lift); v(a, lift);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(P, 3));
  if (uv) g.setAttribute('uv', new THREE.Float32BufferAttribute(U, 2));
  g.computeVertexNormals();
  return g;
}
const polyArea = polys => polys.reduce((s, q) => s + Math.abs(ringArea(q.ring)), 0);
// Scanline footprint ("pseudo road") for earthworks: rows every 0.8 m across the triangles, joined by structure-flagged separators that gradeTile skips.
function inspFootprint(polys, hw = 0.6, step = 0.8) {
  const S = [], boxes = [], R = hw + T.wallStart * T.batter + 2;
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9, zs = 0, zn = 0, bs = 0;
  const rows = [];
  for (const { ring, tris } of polys) for (const t of tris) {
    const [A, B, C] = t.map(i => ring[i]), ymin = Math.min(A[1], B[1], C[1]), ymax = Math.max(A[1], B[1], C[1]);
    for (const p of [A, B, C]) { x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); y0 = Math.min(y0, p[1]); y1 = Math.max(y1, p[1]); zs += p[2]; bs += hBase(p[0], p[1]); zn++; }
    for (let y = Math.ceil(ymin / step) * step; y <= ymax + 1e-9; y += step) {
      const hit = [];
      for (const [P, Q] of [[A, B], [B, C], [C, A]]) {
        if ((P[1] - y) * (Q[1] - y) > 0 || P[1] === Q[1]) continue;
        const u = (y - P[1]) / (Q[1] - P[1]); hit.push([P[0] + (Q[0] - P[0]) * u, P[2] + (Q[2] - P[2]) * u]);
      }
      if (hit.length < 2) continue;
      hit.sort((p, q) => p[0] - q[0]);
      rows.push([{ x: hit[0][0], y, z: hit[0][1] }, { x: hit[hit.length - 1][0], y, z: hit[hit.length - 1][1] }]);
    }
  }
  if (!rows.length) return null;
  const dz = zs / zn - bs / zn, br = dz > T.wallCap, tu = dz < -T.wallCap;   // junction on a viaduct / in a tunnel: not graded
  const mk = (p, f) => ({ x: p.x, y: p.y, z: p.z, h: 0, wL: 0, wR: 0, br: f ? false : br, tu: f ? true : tu, mL: false, mR: false });
  rows.forEach((row, i) => {
    if (i) { S.push(mk(rows[i - 1][1], true), mk(row[0], true)); }
    S.push(mk(row[0]), mk(row[1]));
    boxes.push([Math.min(row[0].x, row[1].x) - R, row[0].y - R, Math.max(row[0].x, row[1].x) + R, row[0].y + R]);
  });
  return { hw, reach: R, bbox: { x0: x0 - R, y0: y0 - R, x1: x1 + R, y1: y1 + R }, samples: S, segBoxes: boxes };
}
function inspectFootprints() { return INSPECT.fp; }   // earthworks.js roadsData() hook

function inspJunction(j, off, grp, fps) {
  const tr = rings => rings.map(r => r.map(p => [p[0] + off.ox, p[1] + off.oy, p[2] == null ? NaN : p[2] + off.oz]));
  const fixZ = rings => rings.forEach(r => r.forEach(p => { if (!Number.isFinite(p[2])) p[2] = inspZ(p); }));
  const get = k => { const rr = tr(inspRings(j[k])); fixZ(rr); return inspTriangulate(rr); };
  const add = (geo, mat, shadow) => { const m = new THREE.Mesh(geo, mat); m.receiveShadow = true; m.castShadow = !!shadow; m.userData.kind = 'junction'; grp.add(m); };
  const cw = get('carriageway'), pv = get('pavements'), isl = get('islands'), gore = get('gore');
  const uvA = p => [p[0] * 0.25, -p[1] * 0.25], uvG = p => [(p[0] + p[1]) * 0.47, 0];
  if (cw.length) add(inspPolyGeo(cw, 0, false, uvA), IM.asph);
  if (pv.length) { add(inspPolyGeo(pv, PAVE_H, true, null), IM.pave); }
  for (const q of isl) add(inspPolyGeo([q], 0.15, true, null), polyArea([q]) > 20 ? IM.grass : IM.paving);
  if (gore.length) add(inspPolyGeo(gore, 0.01, false, uvG), IM.gore);
  for (const set of [cw, pv, isl, gore]) { const f = inspFootprint(set); if (f) fps.push(f); }
  return { cw: cw.length, pv: pv.length, isl: isl.length, gore: gore.length };
}

const TURN_COL = { left: 0x4aa3ff, right: 0xff9f43, straight: 0x3fb950, through: 0x3fb950, ahead: 0x3fb950, uturn: 0xc678dd, u_turn: 0xc678dd, merge: 0xe5484d, diverge: 0x39d0d0 };
function inspConnectors(j, off, grp) {
  let n = 0, nd = 0;
  for (const c of j.connectors || []) {
    if (!c.path || c.path.length < 2) continue;
    const pts = c.path.map(p => { const z = Number.isFinite(p[2]) ? p[2] + off.oz : inspZ([p[0] + off.ox, p[1] + off.oy, NaN]); const w = w3(p[0] + off.ox, p[1] + off.oy, z + 0.4); return new THREE.Vector3(w[0], w[1], w[2]); });
    const g = new THREE.BufferGeometry().setFromPoints(pts), en = c.enabled !== false;
    const col = TURN_COL[String(c.turn || '').toLowerCase()] ?? 0xffd866;
    const m = en ? new THREE.LineBasicMaterial({ color: col }) : new THREE.LineDashedMaterial({ color: 0x8a8f98, dashSize: 1.0, gapSize: 0.8 });
    const line = new THREE.Line(g, m); if (!en) line.computeLineDistances();
    line.renderOrder = 20; grp.add(line); n++; if (!en) nd++;
  }
  return { n, nd };
}

function inspClear() {
  for (const r of roads) touch(r);
  for (const b of INSPECT.boxes) markDirty(b);
  roads = []; INSPECT.fp = []; INSPECT.boxes = [];
  undoStack.length = 0; redoStack.length = 0; state.sel = null; state.hover = null;
  for (const g of [INSPECT.group, INSPECT.conn]) for (const o of [...g.children]) { g.remove(o); if (o.geometry) o.geometry.dispose(); }
}
function loadScene(json, name = '') {
  if (typeof json === 'string') json = JSON.parse(json);
  if (!json || !Array.isArray(json.roads)) throw new Error('not a sim_core scene (no roads[])');
  inspClear();
  const juncs = json.junctions || [], trims = juncs.flatMap(j => j.trims || []);
  // footprint of everything in sim space -> placement
  let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9; const gz = [];
  const grow = (x, y) => { x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); };
  for (const r of json.roads) for (const s of r.samples || []) { grow(s.x, s.y); if (!inspStruct(s.structure)) gz.push(s.z); }
  for (const j of juncs) for (const k of ['carriageway', 'pavements', 'islands', 'gore']) for (const ring of inspRings(j[k])) for (const p of ring) grow(p[0], p[1]);
  if (x0 > x1) throw new Error('scene is empty');
  gz.sort((a, b) => a - b);
  const off = inspPlace({ x0, y0, x1, y1 }, gz.length ? gz[gz.length >> 1] : 0);
  const warn = (x1 - x0 > WORLD - 100 || y1 - y0 > WORLD - 100) ? 'scene exceeds the world size, clamped' : '';
  let nr = 0, br = 0, tu = 0;
  for (const jr of json.roads) { const r = inspRoad(jr, off, trims); if (r) { roads.push(r); touch(r); nr++; br += r.struct.bridge; tu += r.struct.tunnel; } }
  const stats = { cw: 0, pv: 0, isl: 0, gore: 0, conn: 0, off: 0 };
  for (const j of juncs) {
    const s = inspJunction(j, off, INSPECT.group, INSPECT.fp); for (const k of ['cw', 'pv', 'isl', 'gore']) stats[k] += s[k];
    const c = inspConnectors(j, off, INSPECT.conn); stats.conn += c.n; stats.off += c.nd;
  }
  for (const f of INSPECT.fp) { INSPECT.boxes.push({ ...f.bbox }); markDirty(f.bbox); }
  flushGrade();
  INSPECT.conn.visible = INSPECT.showConn;
  const ext = Math.max(x1 - x0, y1 - y0);
  INSPECT.info = { name, cx: off.cx, cy: off.cy, extent: ext, off, roads: nr, junctions: juncs.length, bridgeSamples: br, tunnelSamples: tu, ...stats, warn, drivingSide: json.drivingSide, region: json.region };
  setView(off.cx, off.cy, Math.max(120, ext * 1.1), 0.45, 1.0);
  inspHud(); dirty(); needsRender = true;
  return INSPECT.info;
}
function toggleConnectors() { INSPECT.showConn = !INSPECT.showConn; INSPECT.conn.visible = INSPECT.showConn; inspHud(); dirty(); }

// ---- UI: picker button, drag-and-drop, ?scene= ----
const inspBar = document.createElement('div');
inspBar.style.cssText = 'position:absolute;left:50%;top:12px;transform:translateX(-50%);background:rgba(13,17,23,0.82);border:1px solid #30363d;border-radius:6px;padding:6px 10px;font:12px system-ui;color:#e6edf3;max-width:min(520px,calc(100vw - 520px));text-align:center;z-index:5';
inspBar.innerHTML = '<button id="insp-load" style="font:inherit;cursor:pointer">Load scene JSON…</button> <span id="insp-info" style="color:#8b949e">or drop a file here · J = connectors</span><input id="insp-file" type="file" accept=".json,application/json" style="display:none">';
document.body.appendChild(inspBar);
const inspInfoEl = inspBar.querySelector('#insp-info'), inspFileEl = inspBar.querySelector('#insp-file');
function inspHud(msg, err) {
  const i = INSPECT.info;
  inspInfoEl.style.color = err ? '#ff7b72' : '#8b949e';
  inspInfoEl.textContent = msg || (i ? `${i.name || 'scene'}: ${i.roads} roads, ${i.junctions} junctions, ${i.bridgeSamples} bridge / ${i.tunnelSamples} tunnel samples, ${i.conn} connectors (${i.off} disabled) · J connectors ${INSPECT.showConn ? 'on' : 'off'}${i.warn ? ' · ' + i.warn : ''}` : 'or drop a file here · J = connectors');
}
function inspLoadText(text, name) { try { loadScene(text, name); } catch (e) { inspHud('Load failed: ' + e.message, true); } }
inspBar.querySelector('#insp-load').addEventListener('click', () => inspFileEl.click());
inspFileEl.addEventListener('change', () => { const f = inspFileEl.files[0]; if (f) f.text().then(t => inspLoadText(t, f.name)); inspFileEl.value = ''; });
addEventListener('dragover', e => { e.preventDefault(); });
addEventListener('drop', e => { e.preventDefault(); const f = e.dataTransfer && e.dataTransfer.files[0]; if (f) f.text().then(t => inspLoadText(t, f.name)); });
{
  const q = new URLSearchParams(location.search).get('scene');
  if (q) {
    if (location.protocol === 'file:') inspHud('?scene= needs http (run `python -m http.server` or `npx serve` here); use the button or drop a file', true);
    else fetch(q).then(r => { if (!r.ok) throw new Error(r.status + ' ' + q); return r.text(); }).then(t => inspLoadText(t, q.split('/').pop())).catch(e => inspHud('Fetch failed: ' + e.message, true));
  }
}
window.__inspect = { load: loadScene, toggleConnectors, state: INSPECT };
