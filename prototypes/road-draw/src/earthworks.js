// earthworks.js: road fitting/sampling, classify, and the ONE graded ground surface (GH, 0.5 m grid) computed from all roads together.
// Roads never modify TH (base heights, hBase); they write GH. Meshes elsewhere only draw walls, bridges and portals.
// ===== Roads: fitting, sampling, structure classification =====
let roads = [], nextId = 1;
const undoStack = [], redoStack = [];
let pend = [];   // input.js resets this on failed edits (kept for compat)
const wallLineOf = ty => ty.total / 2;   // retaining wall face: the road's outer edge (wall mesh is 1.0 m thick, outside it)
const WALL_THICK = 1.5;
const VERGE = 1.5;          // flat graded verge beside the road edge (formation level) before the 1:batter slope starts
const WALL_SLACK = 0.75;    // m: a slope may be this much higher than T.wallStart before a wall is needed (marginal need made 28 m wall stubs inside long embankments, with a notch of natural ground beside each); the grading reach still covers it
const WALL_MIN_RUN = 8;     // samples (2 m apart): a wall run is at least 16 m long (>= 15 m)
const WALL_GAP = 8;         // gaps between wall runs shorter than this many samples are bridged (no flicker)
const WING_MAX = 10;       // m: longest wing wall beside a portal / abutment (10 m keeps the funnel face where the slope blends into the wall under 2:1 per texel for the 12.75 m worst case). The approach is an ordinary slope; only the last min(WING_MAX, WING_RATIO * H) metres (H = retained height at the structure) blend into a full wall that meets the headwall
const WING_RATIO = 1.0;     // m of wing wall per m of retained height H (the funnel face between slope and wall then falls at most ~1.5:1 along the road)
const WING_MIN_H = 0.3;     // m: no wing wall where the ground beside the structure is within this of formation
const ROUND_DAYLIGHT = 2.0; // m (horizontal): the slope rolls over into natural ground (top of a cutting, toe of an embankment) over about this run; vertical blend half-width k = ROUND_DAYLIGHT / (2 * batter)
const ROUND_HINGE = 1.0;    // m (horizontal): the verge -> slope hinge is a parabola over this run (shifts the slope outward by half of it)
const ROUND_REACH = ROUND_DAYLIGHT + ROUND_HINGE / 2;   // m: extra grading reach for the rolled-over daylight and the hinge shift
const PLANE_OVER = TUN.planeC;   // m: grading stops at a texel this far past the portal plane (half-plane test per portal, see roadsData / gradeTile): the one-texel ramp up to natural ground then lies fully inside the 2 m thick headwall (and, within the opening, inside the discarded bore); no end cap of an earlier segment can reach further into the bore
const PLANE_MARGIN = 4;     // m: a portal's plane test covers every segment within (road reach + this) of path length from the plane (any segment whose grading can reach the plane's far side)
const WALL_PAD = 5;         // samples: a wall run is padded this far (10 m) beyond the stations that need it, so the wall tapers out before the real need ends
const WALL_RAMP = 11;       // m: beside a wall end, the ground outside the wall face blends from slope to natural over this run (< the 12 m end zone of ground_hidden_by_walls)
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
// Ground as seen by a road with its neighbours' plain slopes in place: terrain, overridden by a neighbour's formation (inside its footprint) or by its batter slope
// (cut: lower, fill: higher). near = [{hw, bbox, S}] (other roads' full samples). Two slopes therefore meet in a valley / ridge between roads, or one lands on the other's verge.
function effGround(x, y, near) {
  const t = hBase(x, y); let g = t;
  for (const q of near) {
    const bb = q.bbox; if (x < bb.x0 || x > bb.x1 || y < bb.y0 || y > bb.y1) continue;
    let bd = 1e18, bz = 0; const S = q.S;
    for (let i = 0; i + 1 < S.length; i++) { const A = S[i], B = S[i + 1]; if (A.br || A.tu || B.br || B.tu) continue;
      const dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy, px = x - A.x, py = y - A.y; let u = L2 > 1e-12 ? (px * dx + py * dy) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
      const ex = px - u * dx, ey = py - u * dy, d2 = ex * ex + ey * ey; if (d2 < bd) { bd = d2; bz = A.z + u * (B.z - A.z) - FORM_DROP; } }
    if (bd > 1e17) continue;
    const e = Math.sqrt(bd) - q.hw;
    if (e <= 0) { g = bz; continue; }
    if (t > bz) { const v = bz + slopeV(e) / T.batter; if (v < g) g = v; } else { const v = bz - slopeV(e) / T.batter; if (v > g) g = v; }
  }
  return g;
}
function needsWall(s, hw, sg, near) {
  const r0 = needsWall0(s, hw, sg, hBase);
  return r0 && near && near.length ? needsWall0(s, hw, sg, (x, y) => effGround(x, y, near)) : r0;   // a wall is only kept if the slope cannot meet the neighbours' earthworks either
}
function needsWall0(s, hw, sg, ground) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP;
  const base = d => ground(s.x + nx * d, s.y + ny * d), g0 = base(hw) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return 0;
  const dMax = hw + VERGE + (T.wallStart + WALL_SLACK) * bt;
  for (let d = hw + 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax), zs = cut ? z0 + slopeV(dd - hw) / bt : z0 - slopeV(dd - hw) / bt; if (cut ? zs >= base(dd) : zs <= base(dd)) return 0; if (dd >= dMax) break; }
  return cut ? 1 : 2;
}
// Structures: bridge / tunnel / portal flags per sample (cut / fill depth beyond the cap; smoothed). Walls are decided separately (computeWalls) because they depend on the neighbouring roads.
function classifyStructures(r, ty) {
  const S = r.samples, n = S.length, hw = ty.total / 2;
  const br = new Array(n).fill(false), tu = br.slice(), mb = br.slice(), mt = br.slice();
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
  r.portals = [];
  for (let i = 0; i < n; i++) {
    const s = S[i]; s.br = s.bridge = br[i]; s.tu = s.tunnel = tu[i];
    s.portal = tu[i] && (i === 0 || !tu[i - 1] || i === n - 1 || !tu[i + 1]);
    if (s.portal) r.portals.push(i);
  }
}
// Walls only where the 1:batter slope (after the verge) cannot daylight within wallStart m of height, nor meet a neighbouring road's earthworks. The tunnel / bridge approach is an
// ordinary slope too; only a short wing wall blends into a full wall right at the portal / abutment so the headwall retains the slope's cross-section there.
// Per side: wall type per sample (1 cut, 2 fill), gaps < WALL_GAP samples bridged, runs shorter than WALL_MIN_RUN grown (never into bridge / tunnel samples), so no short stubs.
function wingLen(S, q, hw, sg) {   // wing wall length (m) beside a structure for the ground sample q next to it, 0 for none
  const s = S[q], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, H = Math.abs(hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK)) - (s.z - FORM_DROP));
  return H < WING_MIN_H ? 0 : Math.min(WING_MAX, WING_RATIO * H);
}
function computeWalls(r, ty, near) {
  const S = r.samples, n = S.length, hw = ty.total / 2;
  const lock = S.map(s => !!(s.br || s.tu)), typ = [new Array(n).fill(0), new Array(n).fill(0)];
  for (const [k, sg] of [[0, 1], [1, -1]]) {
    const t = typ[k], w = new Array(n).fill(false);
    for (let i = 0; i < n; i++) if (!lock[i]) { t[i] = needsWall(S[i], hw, sg, near); w[i] = t[i] > 0; }
    closeGaps(w, WALL_GAP); for (let i = 0; i < n; i++) if (lock[i]) w[i] = false;
    dilate(w, WALL_PAD, lock); growShort(w, WALL_MIN_RUN, lock);
    for (let i = 0; i < n; i++) if (lock[i]) for (const dir of [-1, 1]) {   // wing walls beside every portal / abutment: the first two samples always, then up to the wing length
      const q0 = i + dir; if (q0 < 0 || q0 >= n || lock[q0]) continue;
      const L = wingLen(S, q0, hw, sg); if (L <= 0) continue;
      let d = 0; w[q0] = true;
      for (let q = q0 + dir, cnt = 1; q >= 0 && q < n && !lock[q]; q += dir, cnt++) { d += Math.hypot(S[q].x - S[q - dir].x, S[q].y - S[q - dir].y); if (d > L && cnt >= 2) break; w[q] = true; }
    }
    closeGaps(w, WALL_GAP); for (let i = 0; i < n; i++) if (lock[i]) w[i] = false;
    for (let i = 0; i < n; i++) {
      if (!w[i]) { t[i] = 0; continue; }
      if (t[i] === 0) { const s = S[i], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg; t[i] = hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK)) > s.z - FORM_DROP ? 1 : 2; }   // bridged / grown sample: type from the ground at the wall's outer face
    }
  }
  for (let i = 0; i < n; i++) { const s = S[i]; s.wL = s.wL0 = typ[0][i]; s.wR = s.wR0 = typ[1][i]; }
}
function classify(r, ty) { classifyStructures(r, ty); computeWalls(r, ty, null); }
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
const reachOfRoad = ty => ty.total / 2 + VERGE + (T.wallStart + WALL_SLACK) * T.batter + 2 + ROUND_REACH;
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
  const verge = params.verge, hb = params.roundHinge || 0, rk = params.roundK || 0;
  const sv = e => e <= verge ? 0 : hb > 0 ? (e < verge + hb ? (e - verge) * (e - verge) / (2 * hb) : e - verge - hb / 2) : e - verge;   // verge -> slope hinge: parabola over hb, then the straight slope shifted out by hb/2
  const smax = d => d >= rk ? d : d <= -rk ? 0 : (d + rk) * (d + rk) / (4 * rk);   // smooth max(d, 0) over +-rk: rolls the slope over into natural ground
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
      const ap = rd.planes && rd.planes.length ? rd.planes.filter(pl => k >= pl.k0 && k < pl.k1) : null, pc = params.planeC ?? 1e9;
      for (let j = j0; j <= j1; j++) {
        const ry = y0 + j * res - A.y;
        for (let i = i0; i <= i1; i++) {
          const rx = x0 + i * res - A.x;
          if (ap) { const px = rx + A.x, py = ry + A.y; let beyond = false; for (const pl of ap) if ((px - pl.x) * pl.nx + (py - pl.y) * pl.ny > pc) { beyond = true; break; } if (beyond) continue; }   // past a tunnel portal plane: natural ground (the headwall hides the one-texel step)
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
        const v = zfw + (e - verge) / bt; if (v < b + rk && v < cutM[o]) { cutM[o] = v; cutE[o] = e; }
        continue; }
      if (wf === 2) {
        if (e <= half) { fpD[o] = 0; if (zf < fpZ[o]) fpZ[o] = zf; continue; }
        const zff = zf - ww * (zf - b), v = e <= params.wallThick ? zff : zff - (e - verge) / bt; if (v > b - rk && v > fillM[o]) { fillM[o] = v; fillE[o] = e; }
        continue; }
      if (cutT) { const v = zf + sv(e) / bt; if (v < b + rk && v < cutM[o]) { cutM[o] = v; cutE[o] = e; } }
      else { const v = zf - sv(e) / bt; if (v > b - rk && v > fillM[o]) { fillM[o] = v; fillE[o] = e; } }
    }
  }
  for (let o = 0; o < N; o++) {
    let g;
    if (fpD[o] < 9) g = fpZ[o]; else { g = base[o]; const dc = cutM[o] < 1e8 ? (rk > 0 ? smax(g - cutM[o]) : Math.max(0, g - cutM[o])) : 0, df = fillM[o] > -1e8 ? (rk > 0 ? smax(fillM[o] - g) : Math.max(0, fillM[o] - g)) : 0;   // dip below natural (cut) / rise above it (fill), rounded near daylight
    if (dc > 0 && df > 0) g = cutE[o] <= fillE[o] ? g - dc : g + df; else if (dc > 0) g = g - dc; else if (df > 0) g = g + df; }   // never cross the base to the wrong side: take the nearer of a cut and a fill slope
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
  const hw = ty.total / 2, cap = reachOfRoad(ty), bt = T.batter; let mx = 0;
  const S = r.samples;
  for (let i = 0; i < S.length; i += 2) { const s = S[i]; if (s.br || s.tu) continue;
    const zf = s.z - FORM_DROP;
    const endish = i === 0 || i + 2 >= S.length - 1 || (S[i - 2] && (S[i - 2].br || S[i - 2].tu)) || (S[i + 2] && (S[i + 2].br || S[i + 2].tu));   // road end or next to a bridge / tunnel: the embankment also falls away along the road (cone around the end)
    for (const sg of endish ? [1, -1, 2, 3] : [1, -1]) { if (sg === 1 ? s.wL : sg === -1 ? s.wR : false) continue;
      const nx = sg > 1 ? Math.cos(s.h) * (sg === 2 ? 1 : -1) : -Math.sin(s.h) * sg, ny = sg > 1 ? Math.sin(s.h) * (sg === 2 ? 1 : -1) : Math.cos(s.h) * sg;
      for (let e = 0.5; hw + e <= cap; e += 1) { const b = hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), v = b > zf ? zf + slopeV(e) / bt : zf - slopeV(e) / bt; if (b > zf ? v >= b : v <= b) { if (e > mx) mx = e; break; } if (hw + e + 1 > cap && e > mx) mx = e; } } }
  r._reach = S.some(q => q.br || q.tu) ? cap : Math.min(cap, hw + mx + 2.5 + ROUND_REACH); /* embankment ends at bridges fall away along the road too: full reach */ return r._reach;
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
        const ramp = (free0 && free1) || tot < 1e-6 ? WALL_RAMP : Math.min(WALL_RAMP, tot), d = Math.min(free0 ? cum[k - i] : 1e9, free1 ? tot - cum[k - i] : 1e9) + 1e-9, u = Math.min(1, d / ramp); S[k][key] = u * u * (3 - 2 * u); }   // run end -> 0 (smoothstep)
      i = j; }
  }
}
function roadsData() {
  const rs = roads.filter(r => r.samples && r.samples.length > 1);
  for (const r of rs) { if (!r.bbox) r.bbox = roadBox(r); }
  for (const r of rs) {   // walls with the neighbouring roads' earthworks in place (structures are final here, also for imported roads)
    const near = rs.filter(o => o !== r && !(o.bbox.x1 < r.bbox.x0 || o.bbox.x0 > r.bbox.x1 || o.bbox.y1 < r.bbox.y0 || o.bbox.y0 > r.bbox.y1)).map(o => ({ hw: ROAD_TYPES[o.type].total / 2, bbox: o.bbox, S: o.samples }));
    computeWalls(r, ROAD_TYPES[r.type], near);
  }
  markMedians();
  for (const r of rs) wallWeights(r.samples);
  return roads.filter(r => r.samples && r.samples.length > 1).map(r => { const ty = ROAD_TYPES[r.type]; const R = slopeReach(r, ty), sb = [], full = r.samples, dec = [];
    const pk = a => ({ x: a.x, y: a.y, z: a.z, h: a.h, wL: a.wL, wR: a.wR, wwL: a.wwL, wwR: a.wwR, br: a.br, tu: a.tu, mL: a.mL, mR: a.mR });
    for (let i = 0; i < full.length; i++) { const a = full[i]; if (i % 2 === 0 || i === full.length - 1 || !!full[i - 1].tu !== !!a.tu || !!full[i + 1].tu !== !!a.tu) dec.push(pk(a)); }   // every 2nd sample, plus the last graded / first tunnel sample at each portal (the portal plane stays exact)   // 4 m chords (sag < 0.1 m at minR 20) halve the rasterising cost
    for (let i = 0; i + 1 < dec.length; i += 2) { const a = dec[i], b = dec[Math.min(i + 2, dec.length - 1)]; sb.push([Math.min(a.x, b.x) - R, Math.min(a.y, b.y) - R, Math.max(a.x, b.x) + R, Math.max(a.y, b.y) + R]); }   // coarse reach boxes for tile culling
    const planes = [];   // portal planes: decimated-segment range they cut, plane point, unit normal pointing into the tunnel
    const plen = (a, b) => { let d = 0; for (let q = a; q < b; q++) d += Math.hypot(dec[q + 1].x - dec[q].x, dec[q + 1].y - dec[q].y); return d; }, span = R + PLANE_MARGIN;
    for (let k = 0; k < dec.length; k++) { const a = dec[k], pr = dec[k - 1], nx_ = dec[k + 1]; if (!a.tu) continue;
      if (pr && !pr.tu) { let k0 = k; while (k0 > 0 && plen(k0, k) < span) k0--; planes.push({ k0, k1: k, x: a.x, y: a.y, nx: Math.cos(a.h), ny: Math.sin(a.h) }); }
      if (nx_ && !nx_.tu) { let k1 = k; while (k1 < dec.length - 1 && plen(k, k1) < span) k1++; planes.push({ k0: k, k1, x: a.x, y: a.y, nx: -Math.cos(a.h), ny: -Math.sin(a.h) }); } }
    return { hw: ty.total / 2, reach: R, bbox: r.bbox, samples: dec, segBoxes: sb, planes }; }).concat(typeof inspectFootprints === 'function' ? inspectFootprints() : []);   // inspect.js junction surfaces
}
// Recompute GH inside box {x0,y0,x1,y1} (world metres) from base + all roads. Calls onGroundChanged(box) with the snapped box.
function gradeRegion(box) {
  const gi0 = clamp(Math.floor(box.x0 / GRID_RES), 0, GN - 1), gj0 = clamp(Math.floor(box.y0 / GRID_RES), 0, GN - 1);
  const gi1 = clamp(Math.ceil(box.x1 / GRID_RES), 0, GN - 1), gj1 = clamp(Math.ceil(box.y1 / GRID_RES), 0, GN - 1);
  if (gi1 < gi0 || gj1 < gj0) return;
  const rd = roadsData(), TS = TILE_M / GRID_RES, params = { formDrop: FORM_DROP, batter: T.batter, wallThick: WALL_THICK, verge: VERGE, planeC: PLANE_OVER, roundK: ROUND_DAYLIGHT / (2 * T.batter), roundHinge: ROUND_HINGE };
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
      const wFlag = q => q && (side === 'L' ? q.wL : q.wR) && !q.br && !q.tu;
      if (!(wFlag(s) || (s.tu && s.portal && (wFlag(S[i - 1]) || wFlag(S[i + 1]))))) { cur = null; continue; }   // a portal station joins the wall run beside it: the approach wall runs right up to the headwall plane
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

// ===== Tunnels: bore profile, volume data for the terrain discard, and its JS mirror =====
// A bore is the swept cross-section of the lining's OUTER profile along the road centreline between the two portal planes (first / last tunnel station).
// Profile (all heights above the road level z): flat floor at -floorDepth, vertical sides at lateral +-W up to the springline Hs, then a circular arch of radius R centred at height zc
// (roof(q) = zc + sqrt(R^2 - q^2)). The outer profile is the inner one offset by `off` = lining + margin (concentric: W + off, R + off).
function tunnelProfile(ty) {
  const W = ty.total / 2, rise = TUN.riseK * W, Hs = TUNNEL_H, R = (W * W + rise * rise) / (2 * rise);
  return { W, Hs, Hc: Hs + rise, R, zc: Hs + rise - R, rise };
}
const tunRoof = (p, q, off = 0) => p.zc + Math.sqrt(Math.max((p.R + off) * (p.R + off) - q * q, 0));
// Tunnels of one road: [{a, b}] sample index ranges of contiguous tunnel stations (first / last = portal planes).
function tunnelRuns(r) {
  const S = r.samples || [], n = S.length, out = []; let i = 0;
  while (i < n) { if (!S[i].tu) { i++; continue; } let j = i; while (j + 1 < n && S[j + 1].tu) j++; if (j > i) out.push({ a: i, b: j }); i = j + 1; }
  return out;
}
// Pack every tunnel into the float table the terrain shader reads (tunData, RGBA32F, TUN_W x TUN_MAXC, declared in terrain.js): one row per chunk of <= TUN.chunk segments.
//   texel 0: (min x, min y, min up, nSeg)   texel 1: (max x, max y, max up, 0)         (chunk AABB in plan metres / height)
//   texel 2+2j: (x, y, road z, outer half-width Wd)   texel 3+2j: (arch centre height zc, outer arch radius Rd, end flag (1 = portal plane), 0)   for station j = 0..nSeg
// Returns the number of chunks used.
function packTunnels(data, W, maxRows, vols) {
  let row = 0;
  for (const v of vols) {
    const m = v.st.length, off = TUN.lining + TUN.margin, p = v.prof;
    for (let s0 = 0; s0 + 1 < m && row < maxRows; s0 += TUN.chunk, row++) {
      const s1 = Math.min(m - 1, s0 + TUN.chunk), nSeg = s1 - s0, base = row * W * 4;
      let x0 = 1e18, y0 = 1e18, z0 = 1e18, x1 = -1e18, y1 = -1e18, z1 = -1e18;
      for (let j = 0; j <= nSeg; j++) {
        const st = v.st[s0 + j], o = base + (2 + 2 * j) * 4, wd = p.W + off;
        data[o] = st.x; data[o + 1] = st.y; data[o + 2] = st.z; data[o + 3] = wd;
        data[o + 4] = p.zc; data[o + 5] = p.R + off; data[o + 6] = (s0 + j === 0 || s0 + j === m - 1) ? 1 : 0; data[o + 7] = 0;
        x0 = Math.min(x0, st.x - wd - 1); x1 = Math.max(x1, st.x + wd + 1); y0 = Math.min(y0, st.y - wd - 1); y1 = Math.max(y1, st.y + wd + 1);
        z0 = Math.min(z0, st.z - TUN.floorDepth - 0.1); z1 = Math.max(z1, st.z + p.Hc + off + 0.1);
      }
      data[base] = x0; data[base + 1] = y0; data[base + 2] = z0; data[base + 3] = nSeg; data[base + 4] = x1; data[base + 5] = y1; data[base + 6] = z1; data[base + 7] = 0;
    }
  }
  return row;
}
// JS mirror of the shader's inBore(): is the point (x, y plan, up) inside any packed bore volume? Same maths, same packed table.
function inBoreData(data, W, nRows, x, y, up) {
  for (let c = 0; c < nRows; c++) {
    const b = c * W * 4;
    if (x < data[b] || x > data[b + 4] || y < data[b + 1] || y > data[b + 5] || up < data[b + 2] || up > data[b + 6]) continue;
    const n = data[b + 3];
    for (let i = 0; i < n; i++) {
      const a = b + (2 + 2 * i) * 4, e = a + 8, ax = data[a], ay = data[a + 1], dx = data[e] - ax, dy = data[e + 1] - ay, L2 = dx * dx + dy * dy;
      if (L2 < 1e-9) continue;
      const px = x - ax, py = y - ay, t = (px * dx + py * dy) / L2;
      if (t < (data[a + 6] > 0.5 ? 0 : -0.1) || t > (data[e + 6] > 0.5 ? 1 : 1.1)) continue;
      const lat = (dx * py - dy * px) / Math.sqrt(L2), tc = t < 0 ? 0 : t > 1 ? 1 : t, mx = (u, v) => u + (v - u) * tc;
      const zr = mx(data[a + 2], data[e + 2]), Wd = mx(data[a + 3], data[e + 3]), zc = mx(data[a + 4], data[e + 4]), R = mx(data[a + 5], data[e + 5]), rel = up - zr, al = Math.abs(lat);
      if (al <= Wd && rel >= -TUN.floorDepth && rel <= zc + Math.sqrt(Math.max(R * R - al * al, 0))) return true;
    }
  }
  return false;
}
// All tunnels in the scene as plain data: [{road, a, b, prof, st:[{x,y,z}]}]. Recomputed when roads change (roadmesh syncRoads) and packed into the shader table.
function tunnelVolumes() {
  const out = [];
  for (const r of roads) { if (!r.samples || r.samples.length < 2) continue; const prof = tunnelProfile(ROAD_TYPES[r.type]);
    for (const { a, b } of tunnelRuns(r)) out.push({ road: r, a, b, prof, st: r.samples.slice(a, b + 1).map(s => ({ x: s.x, y: s.y, z: s.z, h: s.h, s: s.s })) }); }
  return out;
}
function refreshTunnels() {
  tunData.fill(0); const vols = tunnelVolumes();
  TUN_STATE.n = packTunnels(tunData, TUN_W, TUN_MAXC, vols); TUN_STATE.vols = vols;
  ringU.uTunN.value = TUN_STATE.n; tunTex.needsUpdate = true; needsRender = true; shadowDirty = true;
}
const tunnelDiscard = (x, y, up) => inBoreData(tunData, TUN_W, TUN_STATE.n, x, y, up);
