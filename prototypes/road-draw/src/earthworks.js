// earthworks.js: road fitting/sampling, classify, applyRoad, regrade, wall line / edge helpers.
// ===== Roads: fitting, sampling, structure classification =====
let roads = [], nextId = 1;
const undoStack = [], redoStack = [];
const edgeOf = ty => ty.total / 2 + SHOULDER + CELL;   // formation edge: everything within this of the centreline is graded flat to formation
const wallLineOf = ty => ty.total / 2 + 0.2;           // retaining wall face: just outside the ribbon (incl. pavement / verge)
const reachOf = ty => edgeOf(ty) + 28 * T.batter;
function closeGaps(a, maxGap) { // fill false runs shorter than maxGap that sit between true samples
  let i = 0; const n = a.length;
  while (i < n) { if (a[i]) { i++; continue; } let j = i; while (j < n && !a[j]) j++; if (i > 0 && j < n && j - i < maxGap) for (let k = i; k < j; k++) a[k] = true; i = j; }
}
function dropShort(a, minRun) {
  let i = 0; const n = a.length;
  while (i < n) { if (!a[i]) { i++; continue; } let j = i; while (j < n && a[j]) j++; if (j - i < minRun) for (let k = i; k < j; k++) a[k] = false; i = j; }
}
// Per sample: base heights at the formation edges, wall flags, bridge / tunnel flags (shared by grader and mesh builder).
function classify(r, ty) {
  const S = r.samples, n = S.length, off = wallLineOf(ty);
  const br = new Array(n).fill(false), tu = new Array(n).fill(false);
  const cL = br.slice(), fL = br.slice(), cR = br.slice(), fR = br.slice();
  for (let i = 0; i < n; i++) {
    const s = S[i], nx = -Math.sin(s.h), ny = Math.cos(s.h), dz = s.z - hBase(s.x, s.y), form = s.z - FORM_DROP;
    s.eL = hBase(s.x + nx * off, s.y + ny * off); s.eR = hBase(s.x - nx * off, s.y - ny * off);   // same (linear mesh) base heights the grader uses
    br[i] = dz > T.wallCap; tu[i] = dz < -T.wallCap;
    cL[i] = s.eL - form > T.wallStart; fL[i] = form - s.eL > T.wallStart;
    cR[i] = s.eR - form > T.wallStart; fR[i] = form - s.eR > T.wallStart;
  }
  for (const a of [br, tu]) { closeGaps(a, 8); dropShort(a, 8); }
  for (let i = 0; i < n; i++) if (br[i] && tu[i]) tu[i] = false;
  for (const a of [cL, fL, cR, fR]) { for (let i = 0; i < n; i++) if (br[i] || tu[i]) a[i] = false; closeGaps(a, 6); dropShort(a, 4); for (let i = 0; i < n; i++) if (br[i] || tu[i]) a[i] = false; }
  for (let i = 0; i < n; i++) {
    const s = S[i]; s.br = s.bridge = br[i]; s.tu = s.tunnel = tu[i];   // shared contract: br/tu (existing mesh code) and bridge/tunnel aliases
    s.wL = cL[i] ? 1 : fL[i] ? 2 : 0; s.wR = cR[i] ? 1 : fR[i] ? 2 : 0;
  }
  // Portal approach: the first 2 tunnel samples at each end of a tunnel run are graded to formation with walls on both sides (no batter),
  // and the grading is pushed on through the ~10 m lining plus one grid cell, so the ground step (up to ~12 m over one 4 m cell) sits behind the lining and never pokes into it.
  const PORTAL_GRADE = 2 + Math.ceil(10 / 2) + 3;
  for (let i = 0; i < n; i++) S[i].tp = false;
  for (let i = 0; i < n; i++) if (tu[i]) {
    let d0 = 0; while (i - d0 - 1 >= 0 && tu[i - d0 - 1]) d0++;
    let d1 = 0; while (i + d1 + 1 < n && tu[i + d1 + 1]) d1++;
    if (d0 < PORTAL_GRADE || d1 < PORTAL_GRADE) { const s = S[i]; s.tp = true; if (!s.wL) s.wL = 1; if (!s.wR) s.wR = 1; }
  }
}
function rebuild(r) {
  const ty = ROAD_TYPES[r.type];
  const ch = G.fitChain(r.pts.map(p => [p.x, p.y]), r.hs, r.he);
  if (!ch) return false;
  const S = [0]; ch.spans.forEach(c => S.push(S[S.length - 1] + c.L));
  r.chain = ch; r.S = S;
  r.vp = G.profile(S, r.pts.map(p => p.z), ty.kc * T.kMul, ty.ks * T.kMul);
  r.samples = G.sample(ch, 2);
  for (const s of r.samples) { s.z = G.profZ(r.vp, s.s); s.g = G.profGrade(r.vp, s.s); s.t = terrainH(s.x, s.y); }
  classify(r, ty);
  r.ver = (r.ver || 0) + 1;
  return true;
}

// ===== Terrain grading (derived from roads; base heights are never lost) =====
const BD = new Float32Array(TN * TN), BS = new Int32Array(TN * TN), BT = new Float32Array(TN * TN), BL = new Int8Array(TN * TN), MK = new Uint8Array(TN * TN);
function roadBBox(r) {
  const reach = reachOf(ROAD_TYPES[r.type]); let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const s of r.samples) { if (s.x < x0) x0 = s.x; if (s.x > x1) x1 = s.x; if (s.y < y0) y0 = s.y; if (s.y > y1) y1 = s.y; }
  return { x0: x0 - reach, y0: y0 - reach, x1: x1 + reach, y1: y1 + reach };
}
const boxHit = (a, b) => a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
function applyRoad(r, rg) {
  const ty = ROAD_TYPES[r.type], S = r.samples, hw = ty.total / 2, reach = reachOf(ty), reach2 = reach * reach, bt = T.batter;
  const bb = roadBBox(r); r.gbb = bb;
  const i0 = Math.max(rg.i0, Math.floor(bb.x0 / CELL)), i1 = Math.min(rg.i1, Math.ceil(bb.x1 / CELL));
  const j0 = Math.max(rg.j0, Math.floor(bb.y0 / CELL)), j1 = Math.min(rg.j1, Math.ceil(bb.y1 / CELL));
  if (i0 > i1 || j0 > j1) return;
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) { const k = j * TN + i; BD[k] = Infinity; MK[k] = 0; }
  for (let k = 0; k + 1 < S.length; k++) {
    const a = S[k], b = S[k + 1], dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy || 1;
    const si0 = Math.max(i0, Math.floor((Math.min(a.x, b.x) - reach) / CELL)), si1 = Math.min(i1, Math.ceil((Math.max(a.x, b.x) + reach) / CELL));
    const sj0 = Math.max(j0, Math.floor((Math.min(a.y, b.y) - reach) / CELL)), sj1 = Math.min(j1, Math.ceil((Math.max(a.y, b.y) + reach) / CELL));
    for (let j = sj0; j <= sj1; j++) for (let i = si0; i <= si1; i++) {
      const x = i * CELL, y = j * CELL, t = clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0, 1);
      const ex = x - (a.x + dx * t), ey = y - (a.y + dy * t), d2 = ex * ex + ey * ey, idx = j * TN + i;
      if (d2 < BD[idx]) { BD[idx] = d2; BS[idx] = k; BT[idx] = t; BL[idx] = (dx * (y - a.y) - dy * (x - a.x)) >= 0 ? 1 : -1; }
    }
  }
  const formAt = idx => { const k = BS[idx], t = BT[idx], a = S[k], b = S[k + 1]; return a.z + (b.z - a.z) * t - FORM_DROP; };
  const sampAt = idx => { const k = BS[idx]; return BT[idx] < 0.5 ? S[k] : S[k + 1]; };
  const edge2 = edgeOf(ty) * edgeOf(ty), edge = edgeOf(ty), wl2 = wallLineOf(ty) * wallLineOf(ty);
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const idx = j * TN + i; if (!(BD[idx] < reach2)) continue;
    const sm = sampAt(idx); if (sm.br || (sm.tu && !sm.tp)) continue;
    const wall = BL[idx] > 0 ? sm.wL : sm.wR, form = formAt(idx), old = TG[idx]; let h = old;
    if (wall) { if (BD[idx] <= wl2) h = form; else continue; }   // walled side: formation up to the wall line, natural beyond (no batter)
    else if (BD[idx] <= edge2) h = form;                  // formation (incl. one extra cell so every triangle under the ribbon is fully under it)
    else { const e = Math.sqrt(BD[idx]) - edge; h = h > form ? Math.min(h, form + e / bt) : Math.max(h, form - e / bt); }
    TG[idx] = h; if (Math.abs(h - old) > 0.02) GR[idx] = 1;
  }
}
function regradeRegion(box) {
  const rg = { i0: Math.max(0, Math.floor(box.x0 / CELL)), i1: Math.min(TN - 1, Math.ceil(box.x1 / CELL)), j0: Math.max(0, Math.floor(box.y0 / CELL)), j1: Math.min(TN - 1, Math.ceil(box.y1 / CELL)) };
  if (rg.i0 > rg.i1 || rg.j0 > rg.j1) return;
  for (let j = rg.j0; j <= rg.j1; j++) for (let i = rg.i0; i <= rg.i1; i++) { const k = j * TN + i; TG[k] = TH[k]; GR[k] = 0; }
  for (const r of roads) if (boxHit(roadBBox(r), box)) { applyRoad(r, rg); r.ver = (r.ver || 0) + 1; }
  updateTerrainMesh(rg);
}
let pend = [];
function touch(r) { if (r.samples) pend.push(roadBBox(r)); if (r.gbb) pend.push(r.gbb); }
function flushGrade() {
  if (!pend.length) return;
  const b = { x0: 1e9, y0: 1e9, x1: -1e9, y1: -1e9 };
  for (const p of pend) { b.x0 = Math.min(b.x0, p.x0); b.y0 = Math.min(b.y0, p.y0); b.x1 = Math.max(b.x1, p.x1); b.y1 = Math.max(b.y1, p.y1); }
  pend = []; regradeRegion(b);
}
const regradeAll = () => { pend = []; regradeRegion({ x0: 0, y0: 0, x1: WORLD, y1: WORLD }); };
