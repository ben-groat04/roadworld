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
function crossSections(r) {
  if (r.xs) return r.xs;
  const ty = ROAD_TYPES[r.type], S = r.samples, n = S.length, hw = ty.total / 2, xs = new Array(n);
  const raw = [[], []], wall = [new Array(n).fill(false), new Array(n).fill(false)];
  for (let i = 0; i < n; i++) {
    const s = S[i];
    if (s.bridge || (s.tunnel && !s.tp)) { raw[0].push(null); raw[1].push(null); continue; }
    for (let q = 0; q < 2; q++) { const m = marchSide(s, hw, q ? -1 : 1, T.wallStart); raw[q].push(m); wall[q][i] = m.kind === 'wall' || !!s.tp; }
  }
  for (let q = 0; q < 2; q++) {
    const w = wall[q], skip = i => S[i].bridge || (S[i].tunnel && !S[i].tp);
    for (let i = 0; i < n; i++) if (skip(i)) w[i] = false;
    closeGaps(w, 6); dropShort(w, 4);
    for (let i = 0; i < n; i++) if (skip(i)) w[i] = false; else if (S[i].tp) w[i] = true;
  }
  for (let i = 0; i < n; i++) {
    const s = S[i], z0 = s.z - FORM_DROP, none = { kind: 'none', edge: hw, z0, d1: hw, z1: z0, cut: false }, out = {};
    for (let q = 0; q < 2; q++) {
      let m = raw[q][i];
      if (!m) m = { ...none };
      else if (wall[q][i] && m.kind !== 'wall') { const sg = q ? -1 : 1, d1 = hw + WALL_DX, z1 = hBase(s.x - Math.sin(s.h) * sg * d1, s.y + Math.cos(s.h) * sg * d1); m = { kind: 'wall', edge: hw, z0, d1, z1, cut: z1 > z0 }; }
      else if (!wall[q][i] && m.kind === 'wall') { m = marchSide(s, hw, q ? -1 : 1, 60); if (m.kind === 'wall') m = { kind: 'slope', edge: hw, z0, d1: hw + 60 * T.batter, z1: m.z1, cut: m.cut }; }
      out[q ? 'R' : 'L'] = m;
    }
    xs[i] = out; s.wL = out.L.kind === 'wall' ? (out.L.cut ? 1 : 2) : 0; s.wR = out.R.kind === 'wall' ? (out.R.cut ? 1 : 2) : 0;   // compat flags
  }
  return r.xs = xs;
}

// ===== Earthwork slope meshes (strip per side, using the terrain material + colour function) =====
function buildEarthworks(r) {
  const g = new THREE.Group(), S = r.samples, xs = crossSections(r), n = S.length, bt = T.batter;
  const pos = [], col = [], idx = [], o = [0, 0, 0];
  for (let q = 0; q < 2; q++) {
    const sg = q ? -1 : 1, key = q ? 'R' : 'L', base = new Array(n).fill(-1);
    for (let i = 0; i < n; i++) {
      const m = xs[i][key]; if (m.kind !== 'slope') continue;
      const s = S[i], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, v0 = pos.length / 3;
      const dExt = m.d1 + 1, zc = m.cut ? m.z1 + 1 / bt : m.z1 - 1 / bt, zExt = Math.min(zc, hBase(s.x + nx * dExt, s.y + ny * dExt) - 0.05);
      for (const [d, z] of [[m.edge, m.z0], [m.d1, m.z1], [dExt, zExt]]) {
        const x = s.x + nx * d, y = s.y + ny * d; pos.push(x, z, -y); terrainColour(x, y, o); col.push(o[0], o[1], o[2]);
      }
      base[i] = v0 / 3;
    }
    for (let i = 0; i + 1 < n; i++) {
      if (base[i] < 0 || base[i + 1] < 0) continue;
      for (let c = 0; c < 2; c++) {
        const a = base[i] + c, b = base[i] + c + 1, a2 = base[i + 1] + c, b2 = base[i + 1] + c + 1;
        for (const t of [[a, a2, b], [b, a2, b2]]) {   // orient each triangle up
          const [p, qq, w] = t.map(k => new THREE.Vector3(pos[k * 3], pos[k * 3 + 1], pos[k * 3 + 2]));
          const nrm = qq.clone().sub(p).cross(w.clone().sub(p));
          if (nrm.y >= 0) idx.push(t[0], t[1], t[2]); else idx.push(t[0], t[2], t[1]);
        }
      }
    }
  }
  if (!idx.length) return g;
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('color', new THREE.Float32BufferAttribute(col, 3));
  geo.setIndex(idx); geo.computeVertexNormals();
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
    const half = m => m.kind === 'slope' ? Math.max(m.edge, m.d1 - 1) : m.d1;
    const on = i => xs[i].L.kind !== 'none' && xs[i].R.kind !== 'none';
    for (let i = 0; i < n;) {
      if (!on(i)) { i++; continue; }
      let j = i; while (j < n && on(j)) j++;
      if (j - i >= 2) {
        c.beginPath();
        for (let k = i; k < j; k++) { const s = S[k], d = half(xs[k].L); c.lineTo(s.x - Math.sin(s.h) * d, s.y + Math.cos(s.h) * d); }
        for (let k = j - 1; k >= i; k--) { const s = S[k], d = half(xs[k].R); c.lineTo(s.x + Math.sin(s.h) * d, s.y - Math.cos(s.h) * d); }
        c.closePath(); c.fill();
      }
      i = j;
    }
  }
  maskTex.needsUpdate = true; maskDirty = false;
}
// Compat wrappers (input.js / ui.js call these): they now just invalidate and redraw the mask.
function touch(r) { maskDirty = true; }
function flushGrade() { if (maskDirty) drawMask(); }
const regradeAll = () => { maskDirty = true; drawMask(); };
