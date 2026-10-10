// earthworks.js: road fitting/sampling, classify, and the ONE graded ground surface (GH, 0.5 m grid) computed from all roads together.
// Roads never modify TH (base heights, hBase); they write GH. Meshes elsewhere only draw walls, bridges and portals.
// ===== Roads: fitting, sampling, structure classification =====
let roads = [], nextId = 1;
const undoStack = [], redoStack = [];
let pend = [];   // input.js resets this on failed edits (kept for compat)
const wallLineOf = ty => ty.total / 2;   // retaining wall face: the road's outer edge (wall mesh is 1.0 m thick, outside it)
const WALL_THICK = 1.0, PZ = 9;
function closeGaps(a, maxGap) { // fill false runs shorter than maxGap that sit between true samples
  let i = 0; const n = a.length;
  while (i < n) { if (a[i]) { i++; continue; } let j = i; while (j < n && !a[j]) j++; if (i > 0 && j < n && j - i < maxGap) for (let k = i; k < j; k++) a[k] = true; i = j; }
}
function dropShort(a, minRun) {
  let i = 0; const n = a.length;
  while (i < n) { if (!a[i]) { i++; continue; } let j = i; while (j < n && a[j]) j++; if (j - i < minRun) for (let k = i; k < j; k++) a[k] = false; i = j; }
}
// Does the slope on side sg (+1 left, -1 right) of sample s need more than wallStart height before it daylights? -> 1 cut wall, 2 fill wall, 0 slope.
function needsWall(s, hw, sg) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP;
  const base = d => hBase(s.x + nx * d, s.y + ny * d), g0 = base(hw) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return 0;
  const dMax = hw + T.wallStart * bt;
  for (let d = hw + 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax), zs = cut ? z0 + (dd - hw) / bt : z0 - (dd - hw) / bt; if (cut ? zs >= base(dd) : zs <= base(dd)) return 0; if (dd >= dMax) break; }
  return cut ? 1 : 2;
}
// Per sample: base heights at the wall lines, wall flags (wL/wR: 1 cut, 2 fill), bridge / tunnel / portal flags.
function classify(r, ty) {
  const S = r.samples, n = S.length, hw = ty.total / 2;
  const br = new Array(n).fill(false), tu = br.slice(), mb = br.slice(), mt = br.slice(), cL = br.slice(), fL = br.slice(), cR = br.slice(), fR = br.slice();
  for (let i = 0; i < n; i++) {
    const s = S[i], nx = -Math.sin(s.h), ny = Math.cos(s.h), dz = s.z - hBase(s.x, s.y), form = s.z - FORM_DROP;
    s.eL = hBase(s.x + nx * hw, s.y + ny * hw); s.eR = hBase(s.x - nx * hw, s.y - ny * hw);
    br[i] = dz > T.wallCap; tu[i] = dz < -T.wallCap;
    // a wall may never exceed wallCap: judge both wall lines, not just the centreline
    mb[i] = br[i] || form - s.eL > T.wallCap || form - s.eR > T.wallCap; mt[i] = tu[i] || s.eL - form > T.wallCap || s.eR - form > T.wallCap;
    br[i] = mb[i]; tu[i] = mt[i] && !mb[i];
  }
  for (const a of [br, tu]) { closeGaps(a, 8); dropShort(a, 8); }
  for (let i = 0; i < n; i++) { if (mb[i]) br[i] = true; else if (mt[i]) tu[i] = true; }   // smoothing may extend, never shrink below the cap rule
  for (let i = 0; i < n; i++) if (br[i] && tu[i]) tu[i] = false;
  for (let i = 0; i < n; i++) if (!br[i] && !tu[i]) {
    const a = needsWall(S[i], hw, 1), b = needsWall(S[i], hw, -1);
    cL[i] = a === 1; fL[i] = a === 2; cR[i] = b === 1; fR[i] = b === 2;
  }
  // tunnel approach: the PZ stations before each portal are walled on both sides (cut)
  for (let i = 0; i < n; i++) if (tu[i]) {
    if (i === 0 || !tu[i - 1]) for (let k = i - 1; k >= Math.max(0, i - PZ) && !tu[k] && !br[k]; k--) { cL[k] = cR[k] = true; fL[k] = fR[k] = false; }
    if (i === n - 1 || !tu[i + 1]) for (let k = i + 1; k <= Math.min(n - 1, i + PZ) && !tu[k] && !br[k]; k++) { cL[k] = cR[k] = true; fL[k] = fR[k] = false; }
  }
  for (const a of [cL, fL, cR, fR]) { for (let i = 0; i < n; i++) if (br[i] || tu[i]) a[i] = false; closeGaps(a, 6); for (let i = 0; i < n; i++) if (br[i] || tu[i]) a[i] = false; }   // runs may only grow
  r.portals = [];
  for (let i = 0; i < n; i++) {
    const s = S[i]; s.br = s.bridge = br[i]; s.tu = s.tunnel = tu[i];
    s.portal = tu[i] && (i === 0 || !tu[i - 1] || i === n - 1 || !tu[i + 1]);
    if (s.portal) r.portals.push(i);
    s.wL = cL[i] ? 1 : fL[i] ? 2 : 0; s.wR = cR[i] ? 1 : fR[i] ? 2 : 0;
  }
}
function rebuild(r) {
  const ty = ROAD_TYPES[r.type];
  const ch = G.fitChain(r.pts.map(p => [p.x, p.y]), r.hs, r.he);
  if (!ch) return false;
  if (r.bbox) markDirty(r.bbox);   // old footprint must be regraded too
  const S = [0]; ch.spans.forEach(c => S.push(S[S.length - 1] + c.L));
  r.chain = ch; r.S = S;
  r.vp = G.profile(S, r.pts.map(p => p.z), ty.kc * T.kMul, ty.ks * T.kMul);
  r.samples = G.sample(ch, 2); r.xs = null;
  for (const s of r.samples) { s.z = G.profZ(r.vp, s.s); s.g = G.profGrade(r.vp, s.s); s.t = terrainH(s.x, s.y); }
  classify(r, ty);
  r.ver = (r.ver || 0) + 1;
  r.bbox = roadBox(r); markDirty(r.bbox);
  return true;
}

// ===== Graded ground: GH (0.5 m grid of final heights) =====
const GRID_RES = 0.5, GN = WORLD / GRID_RES + 1;
const GH = new Float32Array(GN * GN);
const GK = CELL / GRID_RES;   // grid steps per base cell (8)
const REACH_MAX = 17.45 / 2 + 12 * 2 + 3;   // loose upper bound on any road's influence distance (used to pull in neighbouring roads)
const reachOfRoad = ty => ty.total / 2 + T.wallStart * T.batter + 2;
function roadBox(r) {
  const S = r.samples, m = reachOfRoad(ROAD_TYPES[r.type]) + 1; let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const s of S) { if (s.x < x0) x0 = s.x; if (s.x > x1) x1 = s.x; if (s.y < y0) y0 = s.y; if (s.y > y1) y1 = s.y; }
  return { x0: x0 - m, y0: y0 - m, x1: x1 + m, y1: y1 + m };
}
const unionBox = (a, b) => !a ? { ...b } : { x0: Math.min(a.x0, b.x0), y0: Math.min(a.y0, b.y0), x1: Math.max(a.x1, b.x1), y1: Math.max(a.y1, b.y1) };
// base height at grid texel (i, j): identical to hBase (same triangles) but with integer cell maths
function baseIJ(i, j) {
  const ci = Math.min((i / GK) | 0, TN - 2), cj = Math.min((j / GK) | 0, TN - 2), u = (i - ci * GK) / GK, v = (j - cj * GK) / GK, k = cj * TN + ci;
  const a = TH[k], b = TH[k + 1], c = TH[k + TN], d = TH[k + TN + 1];
  return u >= v ? a + u * (b - a) + v * (d - b) : a + u * (d - c) + v * (c - a);
}
for (let j = 0; j < GN; j++) for (let i = 0; i < GN; i++) GH[j * GN + i] = baseIJ(i, j);
function groundAt(x, y) {   // bilinear graded height
  const fx = clamp(x / GRID_RES, 0, GN - 1.0001), fy = clamp(y / GRID_RES, 0, GN - 1.0001), i = fx | 0, j = fy | 0, u = fx - i, v = fy - j, k = j * GN + i;
  return GH[k] * (1 - u) * (1 - v) + GH[k + 1] * u * (1 - v) + GH[k + GN] * (1 - u) * v + GH[k + GN + 1] * u * v;
}
const TILE = 256;   // texels per tile side (128 m)
const tFpD = new Float32Array((TILE + 1) * (TILE + 1)), tFpZ = new Float32Array(tFpD.length), tCut = new Float32Array(tFpD.length), tFill = new Float32Array(tFpD.length);
// Rasterise one segment (a -> b) of a road into the tile scratch. (ti0, tj0) = tile origin texel, (ti1, tj1) inclusive end.
function rasterSeg(A, B, hw, R, ti0, tj0, ti1, tj1) {
  const dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy; if (L2 < 1e-12) return;
  const i0 = Math.max(ti0, Math.floor((Math.min(A.x, B.x) - R) / GRID_RES)), i1 = Math.min(ti1, Math.ceil((Math.max(A.x, B.x) + R) / GRID_RES));
  const j0 = Math.max(tj0, Math.floor((Math.min(A.y, B.y) - R) / GRID_RES)), j1 = Math.min(tj1, Math.ceil((Math.max(A.y, B.y) + R) / GRID_RES));
  const W = TILE + 1, bt = T.batter, R2 = R * R, dz = B.z - A.z, wA = A, wB = B;
  for (let j = j0; j <= j1; j++) {
    const py = j * GRID_RES, ry = py - A.y;
    for (let i = i0; i <= i1; i++) {
      const px = i * GRID_RES, rx = px - A.x;
      let t = (rx * dx + ry * dy) / L2; t = t < 0 ? 0 : t > 1 ? 1 : t;
      const ex = rx - t * dx, ey = ry - t * dy, d2 = ex * ex + ey * ey; if (d2 > R2) continue;
      const dd = Math.sqrt(d2), zf = A.z + t * dz - FORM_DROP, o = (j - tj0) * W + (i - ti0);
      if (dd <= hw) { const q = dd / hw; if (q < tFpD[o]) { tFpD[o] = q; tFpZ[o] = zf; } continue; }
      const e = dd - hw, E = t < 0.5 ? wA : wB, wall = (dx * ry - dy * rx) > 0 ? E.wL : E.wR;   // cross > 0: texel is on the left
      if (wall) { if (e <= WALL_THICK / 2) { const q = dd / hw; if (q < tFpD[o]) { tFpD[o] = q; tFpZ[o] = zf; } } continue; }   // formation up to the wall; the mesh hides the step
      const b = baseIJ(i, j);
      if (b > zf) { const v = zf + e / bt; if (v < b && v < tCut[o]) tCut[o] = v; }
      else { const v = zf - e / bt; if (v > b && v > tFill[o]) tFill[o] = v; }
    }
  }
}
// Recompute GH inside box {x0,y0,x1,y1} (world metres) from base + all roads. Calls onGroundChanged(box) with the snapped box.
function gradeRegion(box) {
  const gi0 = clamp(Math.floor(box.x0 / GRID_RES), 0, GN - 1), gj0 = clamp(Math.floor(box.y0 / GRID_RES), 0, GN - 1);
  const gi1 = clamp(Math.ceil(box.x1 / GRID_RES), 0, GN - 1), gj1 = clamp(Math.ceil(box.y1 / GRID_RES), 0, GN - 1);
  if (gi1 < gi0 || gj1 < gj0) return;
  const W = TILE + 1, live = roads.filter(r => r.samples && r.samples.length > 1);
  for (let tj = gj0; tj <= gj1; tj += TILE) for (let ti = gi0; ti <= gi1; ti += TILE) {
    const ti1 = Math.min(ti + TILE - 1, gi1), tj1 = Math.min(tj + TILE - 1, gj1);
    tFpD.fill(9); tCut.fill(1e9); tFill.fill(-1e9);
    const x0 = ti * GRID_RES - REACH_MAX, x1 = (ti1 + 1) * GRID_RES + REACH_MAX, y0 = tj * GRID_RES - REACH_MAX, y1 = (tj1 + 1) * GRID_RES + REACH_MAX;
    for (const r of live) {
      const bb = r.bbox; if (bb && (bb.x1 < x0 || bb.x0 > x1 || bb.y1 < y0 || bb.y0 > y1)) continue;
      const S = r.samples, ty = ROAD_TYPES[r.type], hw = ty.total / 2, R = reachOfRoad(ty);
      for (let i = 0; i + 1 < S.length; i++) {
        const A = S[i], B = S[i + 1];
        if (A.br || A.tu || B.br || B.tu) continue;
        if (Math.max(A.x, B.x) + R < ti * GRID_RES || Math.min(A.x, B.x) - R > (ti1 + 1) * GRID_RES || Math.max(A.y, B.y) + R < tj * GRID_RES || Math.min(A.y, B.y) - R > (tj1 + 1) * GRID_RES) continue;
        rasterSeg(A, B, hw, R, ti, tj, ti1, tj1);
      }
    }
    for (let j = tj; j <= tj1; j++) for (let i = ti; i <= ti1; i++) {
      const o = (j - tj) * W + (i - ti); let g;
      if (tFpD[o] < 9) g = tFpZ[o];
      else {
        g = baseIJ(i, j);
        if (tCut[o] < g) g = tCut[o];
        if (tFill[o] > g) g = tFill[o];
      }
      GH[j * GN + i] = g;
    }
  }
  const out = { x0: gi0 * GRID_RES, y0: gj0 * GRID_RES, x1: gi1 * GRID_RES, y1: gj1 * GRID_RES };
  if (typeof onGroundChanged === 'function') onGroundChanged(out);
  return out;
}

// ===== Wall runs (consumed by roadmesh) =====
// Contiguous runs at the wall line (road outer edge) per side; a run splits at bridge/tunnel stations and where cut turns to fill.
function wallRuns(r) {
  const S = r.samples, hw = ROAD_TYPES[r.type].total / 2, runs = [];
  if (!S) return runs;
  for (const side of ['L', 'R']) {
    const sg = side === 'L' ? 1 : -1; let cur = null;
    for (let i = 0; i < S.length; i++) {
      const s = S[i];
      if (!(side === 'L' ? s.wL : s.wR) || s.br || s.tu) { cur = null; continue; }
      const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, x = s.x + nx * hw, y = s.y + ny * hw, zf = s.z - FORM_DROP, out = hBase(s.x + nx * (hw + WALL_THICK / 2), s.y + ny * (hw + WALL_THICK / 2));
      const cut = out > zf;
      if (!cur || cur.cut !== cut) { cur = { side, cut, pts: [] }; runs.push(cur); }
      cur.pts.push({ x, y, zTop: cut ? out : zf, zBot: cut ? zf : out, h: s.h });
    }
  }
  return runs;
}

// ===== Undo/regrade plumbing (input.js / ui.js call these) =====
let dirtyBox = null, gradedBox = null;
function markDirty(b) { dirtyBox = unionBox(dirtyBox, b); gradedBox = unionBox(gradedBox, b); }
function touch(r) { if (r && r.samples) { if (!r.bbox) r.bbox = roadBox(r); markDirty(r.bbox); } }
function flushGrade() { if (dirtyBox) { const b = dirtyBox; dirtyBox = null; gradeRegion(b); } }
const regradeAll = () => { for (const r of roads) if (r.samples) r.bbox = roadBox(r); dirtyBox = gradedBox; if (!dirtyBox) return; flushGrade(); gradedBox = null; for (const r of roads) if (r.bbox) gradedBox = unionBox(gradedBox, r.bbox); };
