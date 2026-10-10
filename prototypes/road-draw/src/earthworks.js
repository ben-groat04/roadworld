// earthworks.js: road fitting/sampling, classify, and the ONE graded ground surface (GH, 0.5 m grid) computed from all roads together.
// Roads never modify TH (base heights, hBase); they write GH. Meshes elsewhere only draw walls, bridges and portals.
// ===== Roads: fitting, sampling, structure classification =====
let roads = [], nextId = 1;
const undoStack = [], redoStack = [];
let pend = [];   // input.js resets this on failed edits (kept for compat)
const wallLineOf = ty => ty.total / 2;   // retaining wall face: the road's outer edge (wall mesh is 1.0 m thick, outside it)
const WALL_THICK = 1.5;
const VERGE = 1.5;          // flat graded verge beside the road edge (formation level) before the 1:batter slope starts
const WALL_MIN_RUN = 8;     // samples (2 m apart): a wall run is at least 16 m long (>= 15 m)
const WALL_GAP = 8;         // gaps between wall runs shorter than this many samples are bridged (no flicker)
const PORTAL_WALL = 12;     // samples (24 m): the last stretch before a tunnel portal is walled on both sides (cutting sides taper up into full-height walls that meet the headwall); further out the cutting has ordinary slopes
const PLANE_OVER = 0.6;     // m: grading runs this far past the portal plane, under the 2 m thick headwall, so the bilinear ground at the plane stays at formation
const WALL_PAD = 5;         // samples: a wall run is padded this far (10 m) beyond the stations that need it, so the wall tapers out before the real need ends
const WALL_RAMP = 10;       // m: beside a wall end, the ground outside the wall face blends from slope to natural over this run (< the 12 m end zone of ground_hidden_by_walls)
const slopeV = e => e > VERGE ? e - VERGE : 0;   // slope run beyond the verge
// wall solid spans lateral [hw, hw+WALL_THICK] from the centreline (= wall line hw+0.25, +-0.75..1.25 about it)
function closeGaps(a, maxGap) { // fill false runs shorter than maxGap that sit between true samples
  let i = 0; const n = a.length;
  while (i < n) { if (a[i]) { i++; continue; } let j = i; while (j < n && !a[j]) j++; if (i > 0 && j < n && j - i < maxGap) for (let k = i; k < j; k++) a[k] = true; i = j; }
}
function dropShort(a, minRun) {
  let i = 0; const n = a.length;
  while (i < n) { if (!a[i]) { i++; continue; } let j = i; while (j < n && a[j]) j++; if (j - i < minRun) for (let k = i; k < j; k++) a[k] = false; i = j; }
}
function dilate(a, k, lock) {   // pad true runs by k samples each way (stops at locked samples and array ends)
  const n = a.length, o = a.slice();
  for (let i = 0; i < n; i++) if (a[i]) for (const dir of [-1, 1]) for (let d = 1; d <= k; d++) { const j = i + dir * d; if (j < 0 || j >= n || lock[j]) break; o[j] = true; }
  for (let i = 0; i < n; i++) a[i] = o[i];
}
// Grow every true run shorter than minRun to minRun samples (both ways, never into locked samples). Runs only grow: dropping one would leave a slope that cannot daylight.
function growShort(a, minRun, lock) {
  const n = a.length; let i = 0;
  while (i < n) { if (!a[i]) { i++; continue; } let j = i; while (j < n && a[j]) j++;
    let need = minRun - (j - i), lo = i, hi = j;   // [lo, hi)
    while (need > 0) { const canL = lo > 0 && !lock[lo - 1], canR = hi < n && !lock[hi]; if (!canL && !canR) break;
      if (canL && (!canR || (hi - lo) % 2 === 0)) { lo--; a[lo] = true; } else { a[hi] = true; hi++; } need--; }
    i = Math.max(j, hi); }
}
// Does the slope on side sg (+1 left, -1 right) of sample s need more than wallStart height before it daylights? -> 1 cut wall, 2 fill wall, 0 slope.
function needsWall(s, hw, sg) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP;
  const base = d => hBase(s.x + nx * d, s.y + ny * d), g0 = base(hw) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return 0;
  const dMax = hw + VERGE + T.wallStart * bt;
  for (let d = hw + 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax), zs = cut ? z0 + slopeV(dd - hw) / bt : z0 - slopeV(dd - hw) / bt; if (cut ? zs >= base(dd) : zs <= base(dd)) return 0; if (dd >= dMax) break; }
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
  // Walls only where the 1:batter slope (after the verge) cannot daylight within wallStart m of height. The tunnel approach is NOT walled: the cutting deepens
  // toward the portal with ordinary side slopes (walls appear only where the flank is too steep for them to fit). Per side: wall type per sample
  // (1 cut, 2 fill), gaps < WALL_GAP samples bridged, runs shorter than WALL_MIN_RUN grown (never into bridge/tunnel samples), so no short stubs.
  const lock = br.map((b, i) => b || tu[i]);
  const typ = [new Array(n).fill(0), new Array(n).fill(0)];
  for (const [k, sg] of [[0, 1], [1, -1]]) {
    const t = typ[k], w = new Array(n).fill(false);
    for (let i = 0; i < n; i++) if (!lock[i]) { t[i] = needsWall(S[i], hw, sg); w[i] = t[i] > 0; }
    for (let i = 0; i < n; i++) if (tu[i] || br[i]) {   // portal / abutment approach (the embankment ends against a bridge over falling ground: a slope cannot fit there): walls both sides for PORTAL_WALL samples before the entry / after the exit
      if (i === 0 || !(tu[i] ? tu[i - 1] : br[i - 1])) for (let q = i - 1; q >= Math.max(0, i - PORTAL_WALL) && !lock[q]; q--) w[q] = true;
      if (i === n - 1 || !(tu[i] ? tu[i + 1] : br[i + 1])) for (let q = i + 1; q <= Math.min(n - 1, i + PORTAL_WALL) && !lock[q]; q++) w[q] = true;
    }
    closeGaps(w, WALL_GAP); for (let i = 0; i < n; i++) if (lock[i]) w[i] = false;
    dilate(w, WALL_PAD, lock); growShort(w, WALL_MIN_RUN, lock);
    for (let i = 0; i < n; i++) {
      if (!w[i]) { t[i] = 0; continue; }
      if (t[i] === 0) { const s = S[i], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg; t[i] = hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK)) > s.z - FORM_DROP ? 1 : 2; }   // bridged / grown sample: type from the ground at the wall's outer face
    }
  }
  for (let i = 0; i < n; i++) { cL[i] = typ[0][i] === 1; fL[i] = typ[0][i] === 2; cR[i] = typ[1][i] === 1; fR[i] = typ[1][i] === 2; }
  r.portals = [];
  for (let i = 0; i < n; i++) {
    const s = S[i]; s.br = s.bridge = br[i]; s.tu = s.tunnel = tu[i];
    s.portal = tu[i] && (i === 0 || !tu[i - 1] || i === n - 1 || !tu[i + 1]);
    if (s.portal) r.portals.push(i);
    s.wL = s.wL0 = cL[i] ? 1 : fL[i] ? 2 : 0; s.wR = s.wR0 = cR[i] ? 1 : fR[i] ? 2 : 0;
  }
}
function rebuild(r) {
  if (r.imported) return true;   // inspect.js roads carry their own samples/flags (never refit)
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
// GRID_RES, GN, GH, groundAt and onGroundChanged are declared in terrain.js (loads earlier).
const GK = CELL / GRID_RES;   // grid steps per base cell (8)
const reachOfRoad = ty => ty.total / 2 + VERGE + T.wallStart * T.batter + 2;
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
// ---- Pure core (portable to C++): plain arrays and numbers only, no THREE / DOM / globals. ----
// roadsData: [{ hw, reach, bbox:{x0,y0,x1,y1}, samples:[{x,y,z,h,br,tu,wL,wR}] }]; params: { formDrop, batter, wallThick, verge }.
// baseSampler(x, y) -> base height. Writes nx*ny graded heights to out (row-major, texel (i,j) at x0+i*res, y0+j*res).
function gradeTile(baseSampler, roadsData, params, x0, y0, nx, ny, res, out) {
  const N = nx * ny, bt = params.batter, fd = params.formDrop, half = params.wallThick - res;   // wall band: texels with e <= wallThick-res take the inside (formation) value, so the one-texel ramp to natural lies inside the solid
  const verge = params.verge, sv = e => e > verge ? e - verge : 0;
  const base = new Float32Array(N), fpD = new Float32Array(N).fill(9), fpZ = new Float32Array(N).fill(1e9), cutM = new Float32Array(N).fill(1e9), fillM = new Float32Array(N).fill(-1e9), rqW = new Uint8Array(N), rqWw = new Float32Array(N), rqD = new Float32Array(N), rqZ = new Float32Array(N), cutE = new Float32Array(N), fillE = new Float32Array(N);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) base[j * nx + i] = baseSampler(x0 + i * res, y0 + j * res);
  const tx1 = x0 + (nx - 1) * res, ty1 = y0 + (ny - 1) * res;
  for (const rd of roadsData) {
    const bb = rd.bbox; if (bb && (bb.x1 < x0 || bb.x0 > tx1 || bb.y1 < y0 || bb.y0 > ty1)) continue;
    const S = rd.samples, hw = rd.hw, R = rd.reach, R2 = R * R;
    rqD.fill(1e9);   // per road: nearest-station formation; across roads: lowest wins
    for (let k = 0; k + 1 < S.length; k++) {
      const A = S[k], B = S[k + 1];
      if ((A.br || A.tu) && (B.br || B.tu)) continue;   // bridge / tunnel stations are not graded; a segment with one structure end is graded up to the abutment / portal
      const dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy; if (L2 < 1e-12) continue;
      const i0 = Math.max(0, Math.floor((Math.min(A.x, B.x) - R - x0) / res)), i1 = Math.min(nx - 1, Math.ceil((Math.max(A.x, B.x) + R - x0) / res));
      const j0 = Math.max(0, Math.floor((Math.min(A.y, B.y) - R - y0) / res)), j1 = Math.min(ny - 1, Math.ceil((Math.max(A.y, B.y) + R - y0) / res));
      const dz = B.z - A.z;
      for (let j = j0; j <= j1; j++) {
        const ry = y0 + j * res - A.y;
        for (let i = i0; i <= i1; i++) {
          const rx = x0 + i * res - A.x;
          let t = (rx * dx + ry * dy) / L2; if ((B.tu && !A.tu && t > 1 + PLANE_OVER / Math.sqrt(L2)) || (A.tu && !B.tu && t < -PLANE_OVER / Math.sqrt(L2))) continue;   // tunnel portal: grading stops exactly at the portal plane (the first tunnel station) (the headwall hides the step); bridges keep the rounded embankment end
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          const ex = rx - t * dx, ey = ry - t * dy, d2 = ex * ex + ey * ey; if (d2 > R2) continue;
          const dd = Math.sqrt(d2), o = j * nx + i; if (dd >= rqD[o]) continue;   // nearest station of this road decides (one zf, one wall flag per texel)
          rqD[o] = dd; rqZ[o] = A.z + t * dz - fd; const E = (A.br || A.tu) ? B : (B.br || B.tu) ? A : t < 0.5 ? A : B; const lf = (dx * ry - dy * rx) > 0; rqW[o] = lf ? E.wL : E.wR; { const wa = (lf ? A.wwL : A.wwR) ?? 1, wb = (lf ? B.wwL : B.wwR) ?? 1; rqWw[o] = (A.br || A.tu) ? wb : (B.br || B.tu) ? wa : wa + (wb - wa) * t; }   // cross > 0: texel on the left
        }
      }
    }
    for (let o = 0; o < N; o++) {
      const dd = rqD[o]; if (dd > 1e8) continue; const zf = rqZ[o];
      if (dd <= hw) { fpD[o] = 0; if (zf < fpZ[o]) fpZ[o] = zf; continue; }   // union of footprints: lowest formation wins (never pokes through any road)
      const e = dd - hw, b = base[o], cutT = b > zf; let wf = rqW[o], ww = wf ? rqWw[o] : 0;
      if (wf && (wf === 1) !== cutT) { wf = 0; ww = 0; }   // flag says cut but this texel is fill (or vice versa): plain slope
      // Wall (ww = weight 0..1 along the run; verge == wall thickness so ww = 0 is exactly the plain verge + slope).
      // cut wall: the wall raises the formation level toward natural by ww (hidden behind the road-facing wall face), the slope starts above it.
      // fill wall: the wall's foot sits ww of the way from formation down to natural; the slope continues below the foot.
      if (wf === 1) {
        const zfw = zf + ww * (Math.max(zf, b - 0.1) - zf);
        if (e <= params.wallThick) { fpD[o] = 0; const zw = e <= half ? Math.min(zf, b) : Math.min(zfw, b); if (zw < fpZ[o]) fpZ[o] = zw; continue; }
        const v = zfw + (e - verge) / bt; if (v < b && v < cutM[o]) { cutM[o] = v; cutE[o] = e; }
        continue; }
      if (wf === 2) {
        if (e <= half) { fpD[o] = 0; if (zf < fpZ[o]) fpZ[o] = zf; continue; }
        const zff = zf - ww * (zf - b), v = e <= params.wallThick ? zff : zff - (e - verge) / bt; if (v > b && v > fillM[o]) { fillM[o] = v; fillE[o] = e; }
        continue; }
      if (cutT) { const v = zf + sv(e) / bt; if (v < b && v < cutM[o]) { cutM[o] = v; cutE[o] = e; } }
      else { const v = zf - sv(e) / bt; if (v > b && v > fillM[o]) { fillM[o] = v; fillE[o] = e; } }
    }
  }
  for (let o = 0; o < N; o++) {
    let g;
    if (fpD[o] < 9) g = fpZ[o]; else { g = base[o]; const hc = cutM[o] < g, hf = fillM[o] > g; if (hc && hf) g = cutE[o] <= fillE[o] ? cutM[o] : fillM[o]; else if (hc) g = cutM[o]; else if (hf) g = fillM[o]; }   // never cross the base to the wrong side: take the nearer of a cut and a fill slope
    out[o] = g;
  }
}
// ---- THREE-facing glue: tiles the box into 64 m tiles, feeds gradeTile, writes GH. ----
const TILE_M = 64;
const tileOut = new Float32Array((TILE_M / GRID_RES) * (TILE_M / GRID_RES));
const gridBase = hBaseFine;   // bicubic base (terrain.js), identical to the initial GH fill   // base sampler for grid-aligned texels
// Median sides: a dual carriageway's inner side (facing its opposite-direction, parallel partner of the same type) gets no wall; sets s.mL / s.mR.
function markMedians() {
  const rs = roads.filter(r => r.samples && r.samples.length > 1);
  for (const r of rs) for (const s of r.samples) { s.mL = s.mR = false; if (s.wL0 !== undefined) { s.wL = s.wL0; s.wR = s.wR0; } }
  for (const r of rs) { const ty = ROAD_TYPES[r.type]; if (!ty.dual) continue; const lim = ty.gap + ty.total + 2, bb = r.bbox;
    for (const r2 of rs) { if (r2 === r || r2.type !== r.type) continue; const b2 = r2.bbox; if (bb && b2 && (b2.x1 < bb.x0 || b2.x0 > bb.x1 || b2.y1 < bb.y0 || b2.y0 > bb.y1)) continue;
      for (let i = 0; i < r.samples.length; i += 2) { const s = r.samples[i], nx = -Math.sin(s.h), ny = Math.cos(s.h);
        for (let k = 0; k < r2.samples.length; k += 2) { const t = r2.samples[k], dx = t.x - s.x, dy = t.y - s.y; if (Math.abs(dx) > lim || Math.abs(dy) > lim) continue; const d = Math.hypot(dx, dy); if (d > lim) continue;
          const lat = dx * nx + dy * ny; if (Math.abs(d - Math.abs(lat)) < 2 && Math.cos(t.h - s.h) < -0.7) { if (lat > 0) s.mL = true; else s.mR = true; } } } } }
  const fin = () => { for (const r of rs) for (const s of r.samples) { if (s.mL) s.wL = 0; if (s.mR) s.wR = 0; } };
  for (const r of rs) for (let i = 1; i < r.samples.length; i += 2) { const s = r.samples[i], p = r.samples[i - 1], q = r.samples[Math.min(i + 1, r.samples.length - 1)]; s.mL = p.mL || q.mL || s.mL; s.mR = p.mR || q.mR || s.mR; }
  fin();
}
// Slope reach of road r: farthest lateral distance (from the footprint edge) at which a batter slope still meets the base ground on any unwalled side
// (reach = max |cut/fill| * batter + margin, never more than the wall-start reach). Cached per road version.
function slopeReach(r, ty) {
  if (r._reachVer === r.ver && r._reach) return r._reach;
  const hw = ty.total / 2, cap = reachOfRoad(ty), bt = T.batter; let mx = 0;
  const S = r.samples;
  for (let i = 0; i < S.length; i += 2) { const s = S[i]; if (s.br || s.tu) continue;
    const zf = s.z - FORM_DROP;
    const endish = i === 0 || i + 2 >= S.length - 1 || (S[i - 2] && (S[i - 2].br || S[i - 2].tu)) || (S[i + 2] && (S[i + 2].br || S[i + 2].tu));   // road end or next to a bridge / tunnel: the embankment also falls away along the road (cone around the end)
    for (const sg of endish ? [1, -1, 2, 3] : [1, -1]) { if (sg === 1 ? s.wL : sg === -1 ? s.wR : false) continue;
      const nx = sg > 1 ? Math.cos(s.h) * (sg === 2 ? 1 : -1) : -Math.sin(s.h) * sg, ny = sg > 1 ? Math.sin(s.h) * (sg === 2 ? 1 : -1) : Math.cos(s.h) * sg;
      for (let e = 0.5; hw + e <= cap; e += 1) { const b = hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), v = b > zf ? zf + slopeV(e) / bt : zf - slopeV(e) / bt; if (b > zf ? v >= b : v <= b) { if (e > mx) mx = e; break; } if (hw + e + 1 > cap && e > mx) mx = e; } } }
  r._reach = S.some(q => q.br || q.tu) ? cap : Math.min(cap, hw + mx + 2.5); /* embankment ends at bridges fall away along the road too: full reach */ r._reachVer = r.ver; return r._reach;
}
// Wall weight per sample and side (wwL / wwR in 0..1): 0 at either end of a wall run, 1 further than WALL_RAMP m inside it. The ground OUTSIDE a wall's
// outer face blends from the 1:batter slope (weight 0) to natural ground (weight 1), so a wall end never leaves a bare step beside it. Runs and
// medians are final here (markMedians has already cleared median sides).
function wallWeights(S) {
  const n = S.length;
  for (const [fl, key] of [['wL', 'wwL'], ['wR', 'wwR']]) {
    let i = 0;
    while (i < n) { if (!S[i][fl]) { S[i][key] = 0; i++; continue; } let j = i; while (j < n && S[j][fl]) j++;
      const cum = [0]; for (let k = i + 1; k < j; k++) cum.push(cum[k - i - 1] + Math.hypot(S[k].x - S[k - 1].x, S[k].y - S[k - 1].y));
      const tot = cum[cum.length - 1];
      for (let k = i; k < j; k++) { const free0 = i === 0 || (!S[i - 1].br && !S[i - 1].tu), free1 = j === n || (!S[j].br && !S[j].tu);   // only free run ends (incl. the road's own ends) taper: structures (abutment, portal) hide the wall end
        const d = Math.min(free0 ? cum[k - i] : 1e9, free1 ? tot - cum[k - i] : 1e9) + 1e-9, u = Math.min(1, d / WALL_RAMP); S[k][key] = u * u * (3 - 2 * u); }   // run end -> 0 (smoothstep)
      i = j; }
  }
}
function roadsData() {
  markMedians();
  for (const r of roads) if (r.samples && r.samples.length > 1 && !r.imported) wallWeights(r.samples);
  return roads.filter(r => r.samples && r.samples.length > 1).map(r => { const ty = ROAD_TYPES[r.type]; const R = slopeReach(r, ty), sb = [], full = r.samples, dec = [];
    const pk = a => ({ x: a.x, y: a.y, z: a.z, h: a.h, wL: a.wL, wR: a.wR, wwL: a.wwL, wwR: a.wwR, br: a.br, tu: a.tu, mL: a.mL, mR: a.mR });
    for (let i = 0; i < full.length; i++) { const a = full[i]; if (i % 2 === 0 || i === full.length - 1 || !!full[i - 1].tu !== !!a.tu || !!full[i + 1].tu !== !!a.tu) dec.push(pk(a)); }   // every 2nd sample, plus the last graded / first tunnel sample at each portal (the portal plane stays exact)   // 4 m chords (sag < 0.1 m at minR 20) halve the rasterising cost
    for (let i = 0; i + 1 < dec.length; i += 2) { const a = dec[i], b = dec[Math.min(i + 2, dec.length - 1)]; sb.push([Math.min(a.x, b.x) - R, Math.min(a.y, b.y) - R, Math.max(a.x, b.x) + R, Math.max(a.y, b.y) + R]); }   // coarse reach boxes for tile culling
    return { hw: ty.total / 2, reach: R, bbox: r.bbox, samples: dec, segBoxes: sb }; }).concat(typeof inspectFootprints === 'function' ? inspectFootprints() : []);   // inspect.js junction surfaces
}
// Recompute GH inside box {x0,y0,x1,y1} (world metres) from base + all roads. Calls onGroundChanged(box) with the snapped box.
function gradeRegion(box) {
  const gi0 = clamp(Math.floor(box.x0 / GRID_RES), 0, GN - 1), gj0 = clamp(Math.floor(box.y0 / GRID_RES), 0, GN - 1);
  const gi1 = clamp(Math.ceil(box.x1 / GRID_RES), 0, GN - 1), gj1 = clamp(Math.ceil(box.y1 / GRID_RES), 0, GN - 1);
  if (gi1 < gi0 || gj1 < gj0) return;
  const rd = roadsData(), TS = TILE_M / GRID_RES, params = { formDrop: FORM_DROP, batter: T.batter, wallThick: WALL_THICK, verge: VERGE };
  for (let tj = gj0; tj <= gj1; tj += TS) for (let ti = gi0; ti <= gi1; ti += TS) {
    const nx = Math.min(TS, gi1 - ti + 1), ny = Math.min(TS, gj1 - tj + 1);
    const tx0 = ti * GRID_RES, ty0 = tj * GRID_RES, tx1 = tx0 + nx * GRID_RES, ty1 = ty0 + ny * GRID_RES;
    if (!rd.some(q => q.segBoxes.some(b => b[2] >= tx0 && b[0] <= tx1 && b[3] >= ty0 && b[1] <= ty1))) {   // no road reaches this tile: plain base
      for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) GH[(tj + j) * GN + ti + i] = hBaseFine((ti + i) * GRID_RES, (tj + j) * GRID_RES);
      continue;
    }
    gradeTile(gridBase, rd, params, tx0, ty0, nx, ny, GRID_RES, tileOut);
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) GH[(tj + j) * GN + ti + i] = tileOut[j * nx + i];
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
      const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, x = s.x + nx * hw, y = s.y + ny * hw, zf = s.z - FORM_DROP, out = hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK));
      const cut = out > zf;
      if (!cur || cur.cut !== cut) { cur = { side, cut, lo: 0, hi: WALL_THICK, pts: [] }; runs.push(cur); }
      cur.pts.push({ x, y, zTop: cut ? out : zf, zBot: cut ? zf : out, top: cut ? out + 0.3 : s.z + 1.0, h: s.h });
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
