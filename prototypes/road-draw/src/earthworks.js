// earthworks.js: road fitting/sampling, classify, cross-sections, earthwork meshes, corridor mask. Roads never modify the terrain heightfield.
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
  const mb = br.slice(), mt = br.slice(), cL = br.slice(), fL = br.slice(), cR = br.slice(), fR = br.slice();
  for (let i = 0; i < n; i++) {
    const s = S[i], nx = -Math.sin(s.h), ny = Math.cos(s.h), dz = s.z - hBase(s.x, s.y), form = s.z - FORM_DROP;
    s.eL = hBase(s.x + nx * off, s.y + ny * off); s.eR = hBase(s.x - nx * off, s.y - ny * off);   // same (linear mesh) base heights the grader uses
    br[i] = dz > T.wallCap; tu[i] = dz < -T.wallCap;
    // a wall may never exceed wallCap: judge the whole cross-section (both formation edges), not just the centreline
    mb[i] = br[i] || form - s.eL > T.wallCap || form - s.eR > T.wallCap; mt[i] = tu[i] || s.eL - form > T.wallCap || s.eR - form > T.wallCap;
    br[i] = mb[i]; tu[i] = mt[i] && !mb[i];
    cL[i] = s.eL - form > T.wallStart; fL[i] = form - s.eL > T.wallStart;
    cR[i] = s.eR - form > T.wallStart; fR[i] = form - s.eR > T.wallStart;
  }
  for (const a of [br, tu]) { closeGaps(a, 8); dropShort(a, 8); }
  for (let i = 0; i < n; i++) { if (mb[i]) br[i] = true; else if (mt[i]) tu[i] = true; }   // smoothing may extend, never shrink below the cap rule
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
  r.samples = G.sample(ch, 2); r.xs = null;
  for (const s of r.samples) { s.z = G.profZ(r.vp, s.s); s.g = G.profGrade(r.vp, s.s); s.t = terrainH(s.x, s.y); }
  classify(r, ty);
  r.ver = (r.ver || 0) + 1;
  return true;
}

// ===== Cross-sections: where each road's earthwork meets the (unmodified) base ground =====
const WALL_DX = 0.4;
// One side of one sample. sg = +1 left, -1 right. hLimit = max slope height searched before declaring a wall.
function marchSide(s, hw, sg, hLimit) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP;
  const base = d => hBase(s.x + nx * d, s.y + ny * d);
  const g0 = base(hw) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return { kind: 'slope', edge: hw, z0, d1: hw, z1: z0, cut };
  const f = d => { const zs = cut ? z0 + (d - hw) / bt : z0 - (d - hw) / bt; return cut ? zs - base(d) : base(d) - zs; };
  const dMax = hw + hLimit * bt;
  let lo = hw, hi = -1;
  for (let d = hw + 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax); if (f(dd) >= 0) { hi = dd; break; } lo = dd; if (dd >= dMax) break; }
  if (hi < 0) { const d1 = hw + WALL_DX; return { kind: 'wall', edge: hw, z0, d1, z1: base(d1), cut }; }
  for (let it = 0; it < 10; it++) { const m = (lo + hi) / 2; if (f(m) >= 0) hi = m; else lo = m; }
  return { kind: 'slope', edge: hw, z0, d1: hi, z1: base(hi), cut };
}
// Dual carriageways: the nearest opposite/parallel carriageway of the same dual type on side sg (median gap), else null.
function findMedian(r, ty, s, sg, hw) {
  if (!ty.dual) return null;
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, lim = ty.gap + ty.total + 2; let best = null;
  for (const r2 of roads) {
    if (r2 === r || r2.type !== r.type || !r2.samples) continue;
    for (const t of r2.samples) {
      const dx = t.x - s.x, dy = t.y - s.y; if (Math.abs(dx) > lim || Math.abs(dy) > lim) continue;
      const D = Math.hypot(dx, dy), lat = dx * nx + dy * ny;
      if (D > lim || Math.abs(lat - (ty.gap + ty.total)) > 2 || Math.abs(D - lat) > 1.5 || Math.cos(t.h - s.h) > -0.96) continue;   // opposite direction, heading within ~16 deg
      if (!best || lat < best.lat) best = { lat, z: t.z - FORM_DROP };
    }
  }
  if (!best) return null;
  const z0 = s.z - FORM_DROP;
  return { kind: 'median', edge: hw, z0, d1: best.lat / 2 + 0.3, mid: best.lat / 2, z1: Math.min(z0, best.z), cut: false };
}
// Junctions: stations within (other half-width + 6 m) of another road's centreline at a real angle (crossing / attaching) get no slopes or walls;
// one shared flat platform per crossing (owned by the lower-id road) covers both corridors instead.
function hull2(P) {
  P = P.slice().sort((a, b) => a[0] - b[0] || a[1] - b[1]); if (P.length < 3) return P;
  const cr = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]), lo = [], up = [];
  for (const p of P) { while (lo.length > 1 && cr(lo[lo.length - 2], lo[lo.length - 1], p) <= 0) lo.pop(); lo.push(p); }
  for (let i = P.length - 1; i >= 0; i--) { const p = P[i]; while (up.length > 1 && cr(up[up.length - 2], up[up.length - 1], p) <= 0) up.pop(); up.push(p); }
  lo.pop(); up.pop(); return lo.concat(up);
}
function marchPt(x, y, ux, uy, z0, hLimit) {
  const bt = T.batter, base = d => hBase(x + ux * d, y + uy * d), g0 = base(0.01) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return { d1: 0, z1: z0, cut };
  const f = d => { const zs = cut ? z0 + d / bt : z0 - d / bt; return cut ? zs - base(d) : base(d) - zs; };
  let lo = 0, hi = -1; const dMax = hLimit * bt;
  for (let d = 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax); if (f(dd) >= 0) { hi = dd; break; } lo = dd; }
  if (hi < 0) hi = dMax;
  else for (let it = 0; it < 10; it++) { const m = (lo + hi) / 2; if (f(m) >= 0) hi = m; else lo = m; }
  return { d1: hi, z1: base(hi), cut };
}
function platRing(P) {   // per hull vertex: outward unit, slope toe distance / height, mask toe point (cached)
  if (P.ring) return P.ring;
  const H = P.hull, m = H.length, cx = H.reduce((a, p) => a + p[0], 0) / m, cy = H.reduce((a, p) => a + p[1], 0) / m; P.cx = cx; P.cy = cy;
  // Edges that run across a road's continuation (the platform ends mid-road) get no slope: the road carries on at its own level.
  const flat = new Array(m).fill(false);
  for (let k = 0; k < m; k++) {
    const a = H[k], b = H[(k + 1) % m], mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2; let ex = b[1] - a[1], ey = -(b[0] - a[0]); const l = Math.hypot(ex, ey) || 1; ex /= l; ey /= l;
    if (ex * (mx - cx) + ey * (my - cy) < 0) { ex = -ex; ey = -ey; }
    const hit = (qx, qy) => { for (const r2 of roads) { if (!r2.samples) continue; const h2 = ROAD_TYPES[r2.type].total / 2 + 0.5; for (const t of r2.samples) if (!t.bridge && !t.tunnel && Math.hypot(t.x - qx, t.y - qy) < h2) return true; } return false; };
    if (hit(mx + ex * 1.5, my + ey * 1.5) || hit(mx + ex * 3, my + ey * 3)) flat[k] = flat[(k + 1) % m] = true;
  }
  return P.ring = H.map((p, k) => {
    const a = H[(k + m - 1) % m], b = H[(k + 1) % m]; let ux = b[1] - a[1], uy = -(b[0] - a[0]); const l = Math.hypot(ux, uy) || 1; ux /= l; uy /= l;
    if (ux * (p[0] - cx) + uy * (p[1] - cy) < 0) { ux = -ux; uy = -uy; }
    const z0 = platZ(P, p[0], p[1]), mm = flat[k] ? { d1: 0, z1: z0, cut: false, flat: true } : marchPt(p[0], p[1], ux, uy, z0, 60), e = Math.max(0, mm.d1 - 1);
    return { ux, uy, ...mm, toe: [p[0] + ux * e, p[1] + uy * e] };
  });
}
// Least-squares plane through the junction's road stations, lowered so it never rises above any of them (minus the formation drop): both roads sit on it.
function fitPlane(P) {
  const n = P.length, cx = P.reduce((a, p) => a + p[0], 0) / n, cy = P.reduce((a, p) => a + p[1], 0) / n, cz = P.reduce((a, p) => a + p[2], 0) / n;
  let sxx = 0, sxy = 0, syy = 0, sxz = 0, syz = 0;
  for (const p of P) { const x = p[0] - cx, y = p[1] - cy, z = p[2] - cz; sxx += x * x; sxy += x * y; syy += y * y; sxz += x * z; syz += y * z; }
  const det = sxx * syy - sxy * sxy; let b = 0, c = 0;
  if (Math.abs(det) > 1e-6 * (sxx + syy + 1) * (sxx + syy + 1)) { b = (sxz * syy - syz * sxy) / det; c = (syz * sxx - sxz * sxy) / det; }
  let low = 0; for (const p of P) low = Math.max(low, cz + b * (p[0] - cx) + c * (p[1] - cy) - p[2]);
  return { cx, cy, z: cz - low - FORM_DROP, b, c };
}
const platZ = (P, x, y) => P.pl.z + P.pl.b * (x - P.pl.cx) + P.pl.c * (y - P.pl.cy);
function findJunctions(r, ty, hw) {
  const S = r.samples, n = S.length, jn = new Array(n).fill(false), plats = [];
  for (const r2 of roads) {
    if (r2 === r || !r2.samples) continue;
    const ty2 = ROAD_TYPES[r2.type], hw2 = ty2.total / 2, lim = hw2 + 6, fl = new Array(n).fill(false);
    for (let i = 0; i < n; i++) {
      const s = S[i]; if (s.bridge || s.tunnel) continue;
      for (const t of r2.samples) {
        const dx = t.x - s.x, dy = t.y - s.y; if (Math.abs(dx) > lim || Math.abs(dy) > lim || Math.hypot(dx, dy) > lim) continue;
        if (Math.abs(Math.sin(t.h - s.h)) < 0.34) continue;
        fl[i] = true; break;
      }
    }
    for (let i = 0; i < n; i++) if (fl[i]) jn[i] = true;
    if (r.id > r2.id) continue;
    for (let i = 0; i < n;) {
      if (!fl[i]) { i++; continue; }
      let j = i; while (j < n && fl[j]) j++;
      const pts = [], zs = [], src = [], cl = hw + 6;
      for (let k = i; k < j; k++) { const s = S[k], nx = -Math.sin(s.h), ny = Math.cos(s.h), o = hw + 3; zs.push(s.z); src.push([s.x, s.y, s.z]); pts.push([s.x + nx * o, s.y + ny * o], [s.x - nx * o, s.y - ny * o]); }
      for (const t of r2.samples) {
        if (t.bridge || t.tunnel) continue;
        let near = false; for (let k = i; k < j && !near; k++) near = Math.hypot(S[k].x - t.x, S[k].y - t.y) < cl;
        if (!near) continue;
        const nx = -Math.sin(t.h), ny = Math.cos(t.h), o = hw2 + 3; zs.push(t.z); src.push([t.x, t.y, t.z]); pts.push([t.x + nx * o, t.y + ny * o], [t.x - nx * o, t.y - ny * o]);
      }
      const h = hull2(pts);
      if (h.length >= 3) {
        const pl = fitPlane(src), P = { hull: h, pl }, inside = (x, y) => { let ok = true; for (let a = 0; a < h.length && ok; a++) { const p0 = h[a], p1 = h[(a + 1) % h.length]; if ((p1[0] - p0[0]) * (y - p0[1]) - (p1[1] - p0[1]) * (x - p0[0]) < -1) ok = false; } return ok; };
        let low = 0; for (const rr of [r, r2]) for (const t of rr.samples) if (!t.bridge && !t.tunnel && inside(t.x, t.y)) low = Math.max(low, platZ(P, t.x, t.y) - (t.z - FORM_DROP));
        pl.z -= low + 0.15; plats.push(P);
      }
      i = j;
    }
  }
  return { jn, plats };
}
function crossSections(r) {
  const sig = roads.map(q => q.id + ':' + (q.ver || 0)).join();
  if (r.xs && r.xsSig === sig) return r.xs;
  r.xsSig = sig;
  const ty = ROAD_TYPES[r.type], S = r.samples, n = S.length, hw = ty.total / 2, xs = new Array(n);
  const raw = [[], []], wall = [new Array(n).fill(false), new Array(n).fill(false)];
  const J = findJunctions(r, ty, hw); r.plats = J.plats;
  for (let i = 0; i < n; i++) {
    const s = S[i];
    if (s.bridge || s.tunnel || J.jn[i]) { raw[0].push(null); raw[1].push(null); continue; }
    for (let q = 0; q < 2; q++) {
      const md = findMedian(r, ty, s, q ? -1 : 1, hw);
      if (md) { raw[q].push(md); wall[q][i] = false; continue; }   // median between carriageways: flat fill, never a slope/wall trench
      const m = marchSide(s, hw, q ? -1 : 1, T.wallStart); raw[q].push(m); wall[q][i] = m.kind === 'wall';
    }
  }
  // Tunnel approaches: the ~9 samples before each portal are walled both sides (wing walls from roadmesh take over), so no slope strip ends up inside / short of the wings.
  const near = new Array(n).fill(false), PZ = 9;
  for (let i = 0; i < n; i++) if (S[i].tunnel) {
    if (i === 0 || !S[i - 1].tunnel) for (let k = i - 1; k >= Math.max(0, i - PZ) && !S[k].tunnel && !S[k].bridge; k--) near[k] = true;
    if (i === n - 1 || !S[i + 1].tunnel) for (let k = i + 1; k <= Math.min(n - 1, i + PZ) && !S[k].tunnel && !S[k].bridge; k++) near[k] = true;
  }
  for (let q = 0; q < 2; q++) {
    const w = wall[q], med = i => raw[q][i] && raw[q][i].kind === 'median', skip = i => S[i].bridge || S[i].tunnel || J.jn[i] || med(i);
    for (let i = 0; i < n; i++) if (skip(i)) w[i] = false;
    closeGaps(w, 6); dropShort(w, 4);
    for (let i = 0; i < n; i++) if (skip(i)) w[i] = false; else if (near[i]) w[i] = true;
  }
  for (let i = 0; i < n; i++) {
    const s = S[i], z0 = s.z - FORM_DROP, none = { kind: 'none', edge: hw, z0, d1: hw, z1: z0, cut: false }, out = {};
    for (let q = 0; q < 2; q++) {
      let m = raw[q][i];
      if (!m) m = { ...none };
      else if (m.kind === 'median') { /* keep */ }
      else if (wall[q][i] && m.kind !== 'wall') { const sg = q ? -1 : 1, d1 = hw + WALL_DX, z1 = hBase(s.x - Math.sin(s.h) * sg * d1, s.y + Math.cos(s.h) * sg * d1); m = { kind: 'wall', edge: hw, z0, d1, z1, cut: z1 > z0 }; }
      else if (!wall[q][i] && m.kind === 'wall') { m = marchSide(s, hw, q ? -1 : 1, 60); if (m.kind === 'wall') m = { kind: 'slope', edge: hw, z0, d1: hw + 60 * T.batter, z1: m.z1, cut: m.cut }; }
      out[q ? 'R' : 'L'] = m;
    }
    xs[i] = out; s.wL = out.L.kind === 'wall' ? (out.L.cut ? 1 : 2) : 0; s.wR = out.R.kind === 'wall' ? (out.R.cut ? 1 : 2) : 0;   // compat flags
  }
  return r.xs = xs;
}

// ===== Earthwork slope meshes (strip per side, using the terrain material + colour function) =====
const SLOPE_N = 5, SLOPE_TINT = 0.14;   // slope face rows; crest-to-mid darkening, faded to nothing over the last 2 rows before the toe
function buildEarthworks(r) {
  const g = new THREE.Group(), S = r.samples, xs = crossSections(r), n = S.length, bt = T.batter;
  const pos = [], col = [], idx = [], o = [0, 0, 0], terrN = [];
  const addV = (x, y, z, tint) => { pos.push(x, z, -y); terrainColour(x, y, o); const k = 1 - tint; col.push(o[0] * k, o[1] * k, o[2] * k); return pos.length / 3 - 1; };
  const tri = (a, b, c) => {   // orient each triangle up
    const p = new THREE.Vector3(pos[a * 3], pos[a * 3 + 1], pos[a * 3 + 2]), qq = new THREE.Vector3(pos[b * 3], pos[b * 3 + 1], pos[b * 3 + 2]), w = new THREE.Vector3(pos[c * 3], pos[c * 3 + 1], pos[c * 3 + 2]);
    if (qq.sub(p).cross(w.sub(p)).y >= 0) idx.push(a, b, c); else idx.push(a, c, b);
  };
  // End cap: a vertical curtain under a strip's cross-section so the corridor-mask hole never shows the water / apron below. Ground colours, outward = dir.
  const cap = (v0, R, dir) => {
    const top = [], bot = [];
    for (let k = 0; k < R; k++) {
      const x = pos[(v0 + k) * 3], z = pos[(v0 + k) * 3 + 1], y = -pos[(v0 + k) * 3 + 2];
      top.push(addV(x, y, z, 0)); bot.push(addV(x, y, z - 3, 0));
    }
    for (let k = 0; k + 1 < R; k++) {
      for (const t of [[top[k], top[k + 1], bot[k]], [bot[k], top[k + 1], bot[k + 1]]]) {
        const [a, b, c] = t, ux = pos[b * 3] - pos[a * 3], uy = pos[b * 3 + 1] - pos[a * 3 + 1], uz = pos[b * 3 + 2] - pos[a * 3 + 2], vx = pos[c * 3] - pos[a * 3], vy = pos[c * 3 + 1] - pos[a * 3 + 1], vz = pos[c * 3 + 2] - pos[a * 3 + 2];
        const nx = uy * vz - uz * vy, nz = ux * vy - uy * vx;
        if (nx * dir[0] + nz * -dir[1] >= 0) idx.push(a, b, c); else idx.push(a, c, b);
      }
    }
  };
  for (let q = 0; q < 2; q++) {
    const sg = q ? -1 : 1, key = q ? 'R' : 'L', base = new Array(n).fill(null);
    for (let i = 0; i < n; i++) {
      const m = xs[i][key]; if (m.kind !== 'slope' && m.kind !== 'median') continue;
      const s = S[i], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, v0 = pos.length / 3, rows = [];
      if (m.kind === 'median') {   // flat grass fill at the lower edge height, short slope from the higher edge
        rows.push([m.edge, m.z0, 0], [m.mid, m.z1, 0], [m.d1, m.z1, 0]);
      } else {
        for (let k = 0; k <= SLOPE_N; k++) { const t = k / SLOPE_N, w = k <= SLOPE_N - 2 ? 1 : k === SLOPE_N - 1 ? 0.5 : 0; rows.push([m.edge + (m.d1 - m.edge) * t, m.z0 + (m.z1 - m.z0) * t, SLOPE_TINT * w]); }
        const dExt = Math.max(m.d1 + 1, m.edge + 2.2), zc = m.cut ? m.z1 + 1 / bt : m.z1 - 1 / bt;
        rows.push([dExt, Math.min(zc, hBase(s.x + nx * dExt, s.y + ny * dExt) - 0.05), 0]);
      }
      for (const [d, z, tint] of rows) addV(s.x + nx * d, s.y + ny * d, z, tint);
      terrN.push(v0 + rows.length - 1);
      base[i] = { v0, R: rows.length, t: [Math.cos(s.h), Math.sin(s.h)] };
    }
    for (let i = 0; i < n; i++) {
      const A = base[i]; if (!A) continue;
      const B = i + 1 < n ? base[i + 1] : null, P = i > 0 ? base[i - 1] : null;
      if (!P || P.R !== A.R) cap(A.v0, A.R, [-A.t[0], -A.t[1]]);
      if (!B || B.R !== A.R) { cap(A.v0, A.R, A.t); continue; }
      for (let c = 0; c + 1 < A.R; c++) { const a = A.v0 + c, b = a + 1, a2 = B.v0 + c, b2 = a2 + 1; tri(a, a2, b); tri(b, a2, b2); }
    }
  }
  for (const P of r.plats || []) {   // junction platform: flat fan + 1:batter slope ring to natural ground
    const H = P.hull, m = H.length, R = platRing(P), c0 = addV(P.cx, P.cy, platZ(P, P.cx, P.cy), 0), ring = [];
    for (let k = 0; k < m; k++) {
      const p = H[k], { ux, uy, d1, z1, cut, flat } = R[k], dE = flat ? 0 : d1 + 1, zc = flat ? z1 - 0.4 : cut ? z1 + 1 / bt : z1 - 1 / bt;
      ring.push(addV(p[0], p[1], platZ(P, p[0], p[1]), 0)); addV(p[0] + ux * d1, p[1] + uy * d1, z1, 0);
      terrN.push(addV(p[0] + ux * dE, p[1] + uy * dE, flat ? zc : Math.min(zc, hBase(p[0] + ux * dE, p[1] + uy * dE) - 0.05), 0));
    }
    for (let k = 0; k < m; k++) {
      const a = ring[k], b = ring[(k + 1) % m];
      tri(c0, a, b);
      for (let c = 0; c < 2; c++) { tri(a + c, b + c, a + c + 1); tri(a + c + 1, b + c, b + c + 1); }
    }
  }
  if (!idx.length) return g;
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  geo.setIndex(idx); geo.computeVertexNormals();
  const nrm = geo.attributes.normal;   // overlap row lies on the terrain: shade it with the terrain's own normal so the seam is invisible
  for (const v of terrN) {
    const x = pos[v * 3], y = -pos[v * 3 + 2], sx = (hBase(x + CELL, y) - hBase(x - CELL, y)) / (2 * CELL), sy = (hBase(x, y + CELL) - hBase(x, y - CELL)) / (2 * CELL), inv = 1 / Math.sqrt(sx * sx + sy * sy + 1);
    nrm.setXYZ(v, -sx * inv, inv, sy * inv);
  }
  const m = new THREE.Mesh(geo, groundMat); m.receiveShadow = true; m.castShadow = true; m.userData.road = r; m.userData.pick = false;
  g.add(m); return g;
}

// ===== Corridor mask (terrain hidden under each road's earthwork footprint) =====
let maskDirty = true, pend = [];
function drawMask() {
  const c = maskCtx; c.setTransform(1, 0, 0, 1, 0, 0); c.fillStyle = '#000'; c.fillRect(0, 0, WORLD, WORLD); c.fillStyle = '#fff';
  for (const r of roads) {
    if (!r.samples) continue;
    const S = r.samples, xs = crossSections(r), n = S.length;
    const half = m => m.kind === 'slope' ? Math.max(m.edge + 1.6, m.d1 - 1) : m.d1;
    const on = i => xs[i].L.kind !== 'none' && xs[i].R.kind !== 'none';
    for (let i = 0; i < n;) {
      if (!on(i)) { i++; continue; }
      let j = i; while (j < n && on(j)) j++;
      if (j - i >= 2) {
        c.beginPath();
        const e0 = i === 0 ? 3.2 : 3.2, e1 = 3.2, ex = 1;   // run ends at a road end: extend the footprint 2 m past the last station so no terrain pokes up at the end
        { const s = S[i], d = half(xs[i].L); c.lineTo(s.x - Math.sin(s.h) * d - Math.cos(s.h) * ex * e0, s.y + Math.cos(s.h) * d - Math.sin(s.h) * ex * e0); }
        for (let k = i; k < j; k++) { const s = S[k], d = half(xs[k].L); c.lineTo(s.x - Math.sin(s.h) * d, s.y + Math.cos(s.h) * d); }
        { const s = S[j - 1], d = half(xs[j - 1].L); c.lineTo(s.x - Math.sin(s.h) * d + Math.cos(s.h) * ex * e1, s.y + Math.cos(s.h) * d + Math.sin(s.h) * ex * e1); }
        { const s = S[j - 1], d = half(xs[j - 1].R); c.lineTo(s.x + Math.sin(s.h) * d + Math.cos(s.h) * ex * e1, s.y - Math.cos(s.h) * d + Math.sin(s.h) * ex * e1); }
        for (let k = j - 1; k >= i; k--) { const s = S[k], d = half(xs[k].R); c.lineTo(s.x + Math.sin(s.h) * d, s.y - Math.cos(s.h) * d); }
        { const s = S[i], d = half(xs[i].R); c.lineTo(s.x + Math.sin(s.h) * d - Math.cos(s.h) * ex * e0, s.y - Math.cos(s.h) * d - Math.sin(s.h) * ex * e0); }
        c.closePath(); c.fill();
      }
      i = j;
    }
  }
  for (const r of roads) for (const P of r.plats || []) { c.beginPath(); platRing(P).forEach(q => c.lineTo(q.toe[0], q.toe[1])); c.closePath(); c.fill(); }
  maskTex.needsUpdate = true; maskDirty = false;
}
// Compat wrappers (input.js / ui.js call these): they now just invalidate and redraw the mask.
function touch(r) { maskDirty = true; }
function flushGrade() { if (maskDirty) drawMask(); }
const regradeAll = () => { maskDirty = true; drawMask(); };
