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
const WALL_MAX_H = 3.0;     // m: tallest single retaining wall (wall_max_height). Above that: wall at the cap + slope, else benching (stacked walls), else the cap is exceeded and flagged (over_height)
const BENCH_MIN = 4.5;      // m: minimum flat bench between stacked walls (bench_min_width), measured between the back of the lower wall and the face of the upper one
const WALL_MIN_LEN = 4.0;   // m: a wall run shorter than this becomes a slope (wall_min_length; one panel)
const SAMPLE_STEP = 2.0;    // m: nominal station spacing (adds one step to a run's arc length when measuring it)
const DITCH_D = 0.4;        // m: depth of the shallow ditch at the toe of a cutting slope (toe_ditch_depth; 0 disables)
const DITCH_W = 1.5;        // m: width of that ditch (toe_ditch_width)
const END_BATTER = 4.0;     // horizontal : vertical of the run-out beyond a free road end (end_taper_batter); gentler than the side batter so an embankment / cutting eases into the ground along the road
const REACH_FADE = 8;       // m: a plain slope that has not met the natural ground by the end of the road's reach fades into it over this run (grading reach_fade)
const MAX_TIERS = 2;        // stacked walls per side at most (max_tiers): two tiers separated by a real berm, or the shared structure between two roads (ownerTiers)
const WALL_SLOPE_EXTRA = 15; // m: the slope above / below a wall may run this much farther than a plain slope may before it must daylight (wall_slope_extra)
const BERM_SLOPE = 0.25;    // v/h: the strip between two tiers climbs (cut) / falls (fill) away from the lower wall at this slope (berm_slope), it is not a flat ledge
const FADE_K = 6;           // the strip behind a full tier carves at most FADE_K * (height still to retain) below natural ground: a second tier grows out of natural ground (internal constant)
const WALL_HARD_MAX = 6.0;  // m: a single fill wall may go this high (wall_hard_max); beyond it the station needs a structure (viaduct / half-bridge): needs_structure
const ROCK_BATTER = 0.5;    // h per 1 v of an engineered rock-cut face (rock_cut_batter)
const ROCK_BERM = 3.0;      // m: catch berm every ROCK_BENCH_H of height (rock_berm_width)
const ROCK_BENCH_H = 9.0;   // m: rock-cut face height between berms (rock_bench_height)
const FILL_STEEP = 1.5;     // h per 1 v: the steepest stable fill slope graded where a wall cannot hold the fill (fill_steep_batter)
const STEEP_REACH = 45;     // m: reach limit of a steep fill that never meets the ground
const WALL_VIS_MIN = 0.35;  // m: a stairs wall lower than this is not built (the ground is natural there)
const CORRIDOR_MAX = 90;    // m: terraced sections may extend this far beyond the footprint edge (grading corridor_max)
const TIER_PITCH = WALL_THICK + BENCH_MIN;   // m: lateral distance between neighbouring tiers of a terraced section
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
// Ground as seen by a road with its neighbours' plain slopes in place: terrain, overridden by a neighbour's batter slope (cut: lower, fill: higher). near = [{hw, bbox, S}]
// (other roads' full samples). Two slopes therefore meet in a valley / ridge between roads. Where the walk meets a neighbour's FOOTPRINT instead, the slope only lands on it
// (fits) if it arrives on the right side of that road's formation: a cut slope at or above it, a fill slope at or below it. Otherwise the neighbour sits on a bench the slope would
// pass under (and carve the hillside beyond), so it does not fit and the wall stays.
function nbrNearest(q, x, y) {   // [lateral distance beyond q's footprint edge, q's formation level there], or null
  const bb = q.bbox; if (x < bb.x0 || x > bb.x1 || y < bb.y0 || y > bb.y1) return null;
  let bd = 1e18, bz = 0, ux = 1, uy = 0; const S = q.S;
  for (let i = 0; i + 1 < S.length; i++) { const A = S[i], B = S[i + 1]; if (A.br || A.tu || B.br || B.tu) continue;
    const dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy, px = x - A.x, py = y - A.y; let u = L2 > 1e-12 ? (px * dx + py * dy) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u;
    const ex = px - u * dx, ey = py - u * dy, d2 = ex * ex + ey * ey; if (d2 < bd) { bd = d2; bz = A.z + u * (B.z - A.z) - FORM_DROP; const L = Math.sqrt(L2) || 1; ux = dx / L; uy = dy / L; } }
  return bd > 1e17 ? null : [Math.sqrt(bd) - q.hw, bz, ux, uy];
}
function effGround(x, y, near) {
  const t = hBase(x, y); let g = t;
  for (const q of near) {
    const n = nbrNearest(q, x, y); if (!n) continue;
    const e = n[0], bz = n[1];
    if (e <= 0) { g = bz; continue; }
    if (t > bz) { const v = bz + slopeV(e) / T.batter; if (v < g) g = v; } else { const v = bz - slopeV(e) / T.batter; if (v > g) g = v; }
  }
  return g;
}
function contactDir(x, y, near) {   // unit direction of the neighbour whose footprint contains (x, y)
  for (const q of near) { const n = nbrNearest(q, x, y); if (n && n[0] <= 0) return [n[2], n[3]]; }
  return null;
}
function contactLevel(x, y, near) {   // formation level of the neighbour whose footprint contains (x, y), else null
  for (const q of near) { const n = nbrNearest(q, x, y); if (n && n[0] <= 0) return n[1]; }
  return null;
}
function needsWall(s, hw, sg, near) {
  const r0 = needsWall0(s, hw, sg, hBase);
  return r0 && near && near.length ? needsWall0(s, hw, sg, (x, y) => effGround(x, y, near), (x, y) => contactLevel(x, y, near)) : r0;   // a wall is only kept if the slope cannot land on the neighbours' earthworks either
}
function needsWall0(s, hw, sg, ground, contact) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP;
  const base = d => ground(s.x + nx * d, s.y + ny * d), g0 = base(hw) - z0, cut = g0 > 0;
  if (Math.abs(g0) < 1e-3) return 0;
  const dMax = hw + VERGE + (T.wallStart + WALL_SLACK) * bt;
  for (let d = hw + 0.5; d < dMax + 0.5; d += 0.5) { const dd = Math.min(d, dMax), zs = cut ? z0 + slopeV(dd - hw) / bt : z0 - slopeV(dd - hw) / bt;
    if (contact) { const bz = contact(s.x + nx * dd, s.y + ny * dd); if (bz !== null) return (cut ? zs >= bz : zs <= bz) ? 0 : cut ? 1 : 2; }   // landed on a neighbour's footprint
    if (cut ? zs >= base(dd) : zs <= base(dd)) return 0; if (dd >= dMax) break; }
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
// ---- Wall plans (stacked retaining walls) ----
// A plan is the cross-section of one side beyond the footprint edge (e = lateral distance from it): tier k is a wall whose solid occupies e in [a[k], a[k] + WALL_THICK], its top at height z[k]
// relative to the formation (cut: up, fill: down), at most WALL_MAX_H above the previous tier; between tiers a flat bench; after the last tier either a plain 1:batter slope (daylights),
// natural ground (nat: the last wall reaches the ground at its back face / foot), or a flat plateau up to a neighbouring road (plat: one structure shared by two roads). full = legacy single
// wall that retains the natural ground at its outer face whatever the height (wing walls at structures, and the fallback when the cap cannot be kept: over = true).
// The whole profile shrinks towards the plain verge + slope with the run weight ww (a[] and z[] scale by ww), so a wall run tapers to nothing at its free ends.
// Does the 1:batter slope from level L (starting at lateral s0) daylight within E, or land on a neighbour's footprint the right way? grd(e) / cn(e): ground / neighbour formation at lateral e.
function slopeFits(cut, L, s0, E, bt, grd, cn, from) {
  for (let e = from; e < E + 0.5; e += 0.5) {
    const ee = Math.min(e, E), zs = cut ? L + (ee > s0 ? ee - s0 : 0) / bt : L - (ee > s0 ? ee - s0 : 0) / bt;
    if (cn) { const bz = cn(ee); if (bz !== null) return cut ? zs >= bz : zs <= bz; }   // landed on a neighbour's footprint
    if (cut ? zs >= grd(ee) : zs <= grd(ee)) return true; if (ee >= E) break;
  }
  return false;
}
// Tiers for one side: kind 'none' (a plain slope does it), 'tiers' or 'fail' (cap cannot be kept inside the corridor E). minTiers forces at least that many tiers (zero height ones pad).
function solveTiers(cut, z0, E, bt, grd, cn, minTiers, cap) {
  const a = [], z = []; let L = z0, nat = false;
  for (;;) {
    const s0 = a.length ? a[a.length - 1] + WALL_THICK : VERGE;
    if (a.length >= minTiers && slopeFits(cut, L, s0, a.length ? E + WALL_SLOPE_EXTRA : E, bt, grd, cn, a.length ? s0 : 0.5)) return a.length ? { kind: 'tiers', a, z, nat: false } : { kind: 'none' };
    if (a.length >= MAX_TIERS) return { kind: 'fail' };
    const amin = a.length ? a[a.length - 1] + WALL_THICK + BENCH_MIN : 0; if (amin + WALL_THICK > E) return { kind: 'fail' };
    const rise = cut ? grd(amin + WALL_THICK) - L : L - grd(amin + WALL_THICK);
    if (rise <= cap + 1e-9) {   // this wall reaches the natural ground at its back face (cut) / foot (fill): natural ground carries on beyond it
      const h = rise > 0.05 ? rise : 0; L = cut ? L + h : L - h; a.push(amin); z.push(L - z0); nat = true;
      if (a.length === 1 && z[0] === 0 && minTiers === 0) return { kind: 'none' };
      return { kind: 'tiers', a, z, nat };
    }
    L = cut ? L + cap : L - cap; a.push(amin); z.push(L - z0);
  }
}
// ---- Terraced sections (stairs) ----
// Walls of height h at the lateral pitch TIER_PITCH, each retaining the ground up to the next bench. The section is evaluated on the spot from the natural ground N(e) along
// the normal (e = lateral distance beyond the footprint edge), so it is a continuous function of the ground and of the formation level zf: a wall run starts or ends where the
// ground says so, never at an arbitrary station. Tier k: solid at e in [k * P, k * P + T], front (road side) level f, top (back) level u. The last tier reaches the ground at
// its back face (eps = 0) and natural ground carries on beyond it. Cut: tiers rise (u > f); fill: they fall. Between two tiers the bench is flat at u, but never more than
// STAIR_K * eps below natural ground (eps = ground still unretained above u): where the need first appears the bench is still natural ground and the next wall grows from nothing.
// force: the last permitted tier takes whatever is left (the hard limit: tier count or corridor): flagged over_height.
const svTop = e => e <= VERGE ? 0 : ROUND_HINGE > 0 ? (e < VERGE + ROUND_HINGE ? (e - VERGE) * (e - VERGE) / (2 * ROUND_HINGE) : e - VERGE - ROUND_HINGE / 2) : e - VERGE;   // the grading's verge -> slope hinge (gradeTile sv)
function stairProfile(cut, zf, h, N, mt) {   // mt: tiers allowed (1 or 2); ok = the last tier reaches the ground
  const sg = cut ? 1 : -1, T = WALL_THICK, tiers = []; let F = zf;
  for (let k = 0; k < mt; k++) {
    const a = k * TIER_PITCH, eb = a + T;
    const rise = sg * (N(eb) - F);
    if (rise <= 0) return { tiers, ok: true };   // natural ground at the back face is at or below the front level: nothing to retain here
    if (rise <= h) { tiers.push({ a, f: F, u: F + sg * rise, eps: 0 }); return { tiers, ok: true }; }
    const u = F + sg * h, eps = rise - h;
    tiers.push({ a, f: F, u, eps });
    if (k === mt - 1) return { tiers, ok: false };
    const e1 = (k + 1) * TIER_PITCH, n1 = N(e1), strip = u + sg * BERM_SLOPE * (e1 - eb);
    F = cut ? Math.min(n1, Math.max(strip, n1 - FADE_K * eps)) : Math.max(n1, Math.min(strip, n1 + FADE_K * eps));
  }
  return { tiers, ok: false };
}
// Engineered rock-cut face (cut side): verge, rock-trap ditch, then a face at ROCK_BATTER (h per v) rising ROCK_BENCH_H, a ROCK_BERM catch berm, and so on; natural ground clips it.
// Steep fill (needs_structure): a plain slope at FILL_STEEP.
function surfZ(kind, cut, e, zf) {
  if (kind === 'rock') {
    if (e <= VERGE) return zf;
    const e0 = VERGE + DITCH_W;
    if (e < e0) { const u = Math.sin(Math.PI * (e - VERGE) / DITCH_W); return zf - 1.5 * DITCH_D * u * u; }
    const d = e - e0, per = ROCK_BENCH_H * ROCK_BATTER + ROCK_BERM, n = Math.floor(d / per), r = d - n * per, fh = ROCK_BENCH_H * ROCK_BATTER;
    return zf + n * ROCK_BENCH_H + (r <= fh ? r / ROCK_BATTER : ROCK_BENCH_H);
  }
  return zf - svTop(e) / FILL_STEEP;
}
// A plan evaluated on the spot: kind 'terrace' (walls, run = {kind, h, mt}), 'rock' (cut face, no walls) or 'sfill' (steep fill slope, no walls). own: what this station alone needs.
function profilePlan(cut, z0, N, run, own) {
  const sg = cut ? 1 : -1; let reachE = 0, n = 0;
  if (run.kind === 'terrace') { const pr = stairProfile(cut, z0, run.h, N, run.mt).tiers; n = pr.length; reachE = n ? pr[n - 1].a + WALL_THICK : 0; }
  else { const lim = run.kind === 'sfill' ? Math.min(CORRIDOR_MAX, STEEP_REACH) : CORRIDOR_MAX; for (let e = 0.5; e <= lim; e += 1) { reachE = e; const z = surfZ(run.kind, cut, e, z0); if (cut ? z >= N(e) : z <= N(e)) break; } reachE += 2; }
  return { cut, n, a: [], z: [], plat: false, nat: false, over: run.kind === 'terrace' && run.h > WALL_MAX_H + 1e-9, full: false, stairs: true, kind: run.kind, own, h: run.h || 0, mt: run.mt || 0, rock: run.kind === 'rock', needs: own === 'sfill', reachE, alongMax: Math.max(0, sg * (N(WALL_THICK) - z0)) * Math.max(END_BATTER, 1) };
}
// The candidate plan of one station when one wall and a slope do not do (preference: two tiers <= cap; fill: one wall <= wall_hard_max; else rock cut / steep fill).
function stairsPlan(cut, z0, N) {
  const t2 = { kind: 'terrace', h: WALL_MAX_H, mt: MAX_TIERS };
  if (stairProfile(cut, z0, t2.h, N, t2.mt).ok) return profilePlan(cut, z0, N, t2, 'cap');
  if (!cut) { const t1 = { kind: 'terrace', h: WALL_HARD_MAX, mt: 1 }; if (stairProfile(false, z0, t1.h, N, 1).ok) return profilePlan(false, z0, N, t1, 'hard'); return profilePlan(false, z0, N, { kind: 'sfill' }, 'sfill'); }
  return profilePlan(true, z0, N, { kind: 'rock' }, 'rock');
}
// A run (contiguous stations, same side and cut) keeps ONE treatment: rock cut / steep fill if any station needs it, else one tall wall if any needs it, else two capped tiers.
function runTreatment(plans) {
  const owns = plans.map(q => q.own);
  if (owns.includes('rock')) return { kind: 'rock' };
  if (owns.includes('sfill')) return { kind: 'sfill' };
  if (owns.includes('hard')) return { kind: 'terrace', h: WALL_HARD_MAX, mt: 1 };
  return { kind: 'terrace', h: WALL_MAX_H, mt: MAX_TIERS };
}
// One structure between two roads at different levels, built by the lower one: tiers rise from its formation to the neighbour's at gap ec.
function ownerTiers(Dz, ec) {
  const nReq = Math.max(1, Math.ceil(Dz / WALL_MAX_H - 1e-9)); let nFit = 0;
  for (let n = 1; n <= MAX_TIERS; n++) if ((n - 1) * (WALL_THICK + BENCH_MIN) + WALL_THICK <= ec + 1e-9) nFit = n;
  if (nFit < 1) return null;
  const n = Math.min(nReq, nFit), h = Dz / n, a = [], z = [];
  for (let k = 0; k < n; k++) { a.push(k * (WALL_THICK + BENCH_MIN)); z.push((k + 1) * h); }
  return { cut: true, n, a, z, plat: true, nat: false, over: nReq > nFit, full: false };
}
// The plan for side sg of sample s: null for a plain slope. cutSet (null | bool) fixes cut / fill for padded samples, minTiers > 0 forces tiers there.
function planSide(s, hw, sg, near, cutSet, minTiers) {
  const nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, bt = T.batter, z0 = s.z - FORM_DROP, E = VERGE + (T.wallStart + WALL_SLACK) * bt;
  const gB = e => hBase(s.x + nx * (hw + e), s.y + ny * (hw + e));
  let cut;
  if (cutSet === null) { const g0 = gB(0) - z0; if (Math.abs(g0) < 1e-3) return null; cut = g0 > 0; } else cut = cutSet;
  const r0 = solveTiers(cut, z0, E, bt, gB, null, minTiers, WALL_MAX_H);
  const hasNear = !!(near && near.length);
  if (r0.kind === 'none' && !(hasNear && cutSet === null)) return null;
  let r = r0, owner = null, over = false;
  // the cap is never escalated: when it cannot be kept inside the corridor the single legacy wall is used and flagged (a staged escalation to taller stacked walls was tried: it made the wall transitions along a run rougher)
  const solveAt = (g, c) => { for (const m of [1]) { const q = solveTiers(cut, z0, E, bt, g, c, minTiers, WALL_MAX_H * m); if (q.kind !== 'fail') { over = m > 1; return q; } } over = true; return { kind: 'fail' }; };
  if (r.kind === 'fail') r = solveAt(gB, null);
  if (hasNear) {   // a wall is only kept if the slope cannot land on the neighbours' earthworks either; a neighbour at another level can also ask for one (shared structure below)
    const gE = e => effGround(s.x + nx * (hw + e), s.y + ny * (hw + e), near), cn = e => contactLevel(s.x + nx * (hw + e), s.y + ny * (hw + e), near);
    if (cutSet === null) for (let e = 0.5; e < E + 0.5; e += 0.5) { const ee = Math.min(e, E), bz = cn(ee); if (bz !== null) { const cd = contactDir(s.x + nx * (hw + ee), s.y + ny * (hw + ee), near); if (cd && Math.abs(cd[0] * Math.cos(s.h) + cd[1] * Math.sin(s.h)) >= 0.9) owner = { ec: ee, bz, nat: gB(ee) }; break; } if (ee >= E) break; }   // only a roughly parallel neighbour shares a structure (a crossing road is a junction)
    if (r0.kind === 'none' && (!owner || Math.abs(owner.bz - z0) < 0.25)) return null;   // bare ground needs no wall: only a neighbour at another level (a real height difference) can ask for one
    over = false; r = solveAt(gE, cn);
    if (r.kind === 'none') return null;
  }
  if (owner) {   // contact with a neighbour the slope cannot reach: ONE structure for the pair, built by the lower road; the upper road leaves its side as a plain slope
    const Dz = owner.bz - z0;
    // the lower road builds it when it is itself cut into the ground on this side; when it stands on fill / level ground it needs no wall of its own and the upper road retains its embankment as usual
    if (Math.abs(Dz) >= 0.25) { if (Dz > 0) return ownerTiers(Dz, owner.ec); if (ownerTiers(-Dz, owner.ec)) return { sup: true }; /* the lower road builds the structure up to this road: nothing to do on this side */ if (owner.nat > owner.bz + 0.25) return null; }
  }
  if (r.kind === 'fail' || (r.kind === 'tiers' && r.a.length >= 2)) return stairsPlan(cut, z0, gB);   // one wall and a slope cannot keep the cap: terraces (as many capped tiers as the ground needs; the tier height grows only where a hard limit is hit)
  return { cut, n: r.a.length, a: r.a, z: r.z, plat: false, nat: r.nat, over, full: false };
}
function computeWalls(r, ty, near) {
  const S = r.samples, n = S.length, hw = ty.total / 2;
  const lock = S.map(s => !!(s.br || s.tu)), typ = [new Array(n).fill(0), new Array(n).fill(0)], plans = [new Array(n).fill(null), new Array(n).fill(null)], sups = [new Array(n).fill(0), new Array(n).fill(0)];
  for (const [k, sg] of [[0, 1], [1, -1]]) {
    const t = typ[k], pl = plans[k], w = new Array(n).fill(false), wing = new Array(n).fill(false);
    const sup = sups[k], needRaw = new Array(n).fill(false);
    for (let i = 0; i < n; i++) if (!lock[i]) { pl[i] = planSide(S[i], hw, sg, near, null, 0); if (pl[i] && pl[i].sup) { sup[i] = 1; pl[i] = null; } t[i] = pl[i] ? (pl[i].cut ? 1 : 2) : 0; w[i] = t[i] > 0; needRaw[i] = w[i]; }
    closeGaps(w, WALL_GAP); for (let i = 0; i < n; i++) if (lock[i]) w[i] = false;
    dilate(w, WALL_PAD, lock); growShort(w, WALL_MIN_RUN, lock);
    for (let i = 0; i < n; i++) if (lock[i]) for (const dir of [-1, 1]) {   // wing walls beside every portal / abutment: the first two samples always, then up to the wing length
      const q0 = i + dir; if (q0 < 0 || q0 >= n || lock[q0]) continue;
      const L = wingLen(S, q0, hw, sg); if (L <= 0) continue;
      let d = 0; w[q0] = true; wing[q0] = true;
      for (let q = q0 + dir, cnt = 1; q >= 0 && q < n && !lock[q]; q += dir, cnt++) { d += Math.hypot(S[q].x - S[q - dir].x, S[q].y - S[q - dir].y); if (d > L && cnt >= 2) break; w[q] = true; wing[q] = true; }
    }
    closeGaps(w, WALL_GAP); for (let i = 0; i < n; i++) if (lock[i]) w[i] = false;
    dropShortLen(w, S, WALL_MIN_LEN, needRaw);   // a wall run shorter than one panel is a slope (unless the ground needs a wall there: a slope would not daylight)
    for (let i = 0; i < n; i++) {
      if (!w[i]) { t[i] = 0; pl[i] = null; continue; }
      if (t[i] === 0) { const s = S[i], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg; t[i] = hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK)) > s.z - FORM_DROP ? 1 : 2; }   // bridged / grown sample: type from the ground at the wall's outer face
    }
    // plans of the padded samples (no need of their own): at least one tier. Tier counts change along a run where the retained height crosses a multiple of the cap, so a new tier starts at zero height.
    for (let i = 0; i < n;) {
      if (!w[i]) { i++; continue; } let j = i; while (j < n && w[j] && t[j] === t[i]) j++;
      for (let q = i; q < j; q++) {
        if (wing[q]) continue;
        if (pl[q]) continue;
        pl[q] = planSide(S[q], hw, sg, near, t[q] === 1, 1);
        if (!pl[q]) pl[q] = { cut: t[q] === 1, n: 0, a: [], z: [], plat: false, nat: false, over: false, full: true };
      }
      for (let q = i; q < j; q++) if (!wing[q]) t[q] = pl[q].cut ? 1 : 2;
      i = j;
    }
    // one tier height per terraced run (a continuous run keeps one set of benches); the hard-limit flag is shared too
    for (let i = 0; i < n;) {
      const p0 = w[i] ? pl[i] : null; if (!(p0 && p0.stairs)) { i++; continue; }
      let j = i; const own = []; while (j < n && w[j] && pl[j] && pl[j].stairs && pl[j].cut === p0.cut) { own.push(pl[j]); j++; }
      const run = runTreatment(own);
      for (let q = i; q < j; q++) { const s = S[q], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, z0 = s.z - FORM_DROP; pl[q] = profilePlan(p0.cut, z0, e => hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), run, pl[q].own); }
      i = j;
    }
    // beyond either end of a terraced run the neighbouring stations (one wall and a slope, or a single wall to the ground) blend into the terraces over WALL_RAMP: ground = alt + sw * (terraces - alt)
    { const near = new Array(n).fill(1e9);
      for (let pass = 0; pass < 2; pass++) for (let q = pass ? n - 1 : 0; pass ? q >= 0 : q < n; q += pass ? -1 : 1) {
        if (w[q] && !wing[q] && pl[q] && pl[q].stairs) near[q] = 0; else { const pq = q - (pass ? -1 : 1); if (pq >= 0 && pq < n && near[pq] < 1e8) near[q] = Math.min(near[q], near[pq] + Math.abs(S[q].s - S[pq].s)); } }
      for (let q = 0; q < n; q++) { const alt = pl[q]; if (!(w[q] && !wing[q] && alt && !alt.stairs && !alt.full && !alt.plat && near[q] > 0 && near[q] < WALL_RAMP)) continue;
        let src = -1, bd = 1e9; for (let j = 0; j < n; j++) if (w[j] && !wing[j] && pl[j] && pl[j].stairs && pl[j].cut === alt.cut && Math.abs(S[j].s - S[q].s) < bd) { bd = Math.abs(S[j].s - S[q].s); src = j; }
        if (src < 0) continue;
        const u = 1 - near[q] / WALL_RAMP, s = S[q], nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg;
        pl[q] = Object.assign(profilePlan(alt.cut, s.z - FORM_DROP, e => hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), { kind: pl[src].kind, h: pl[src].h, mt: pl[src].mt }, pl[src].own), { alt, sw: u * u * (3 - 2 * u) }); } }
    // Wing walls beside a structure: legacy full wall (retains the natural ground at its outer face, whatever the height) blended in from the ordinary earthworks next to it
    // (plain slope, or the adjacent run's tiers = base) by the funnel weight fw, 0 at the far end of the wing to 1 at the structure (smoothstep over min(WALL_RAMP, length)).
    for (let i = 0; i < n;) {
      if (!(w[i] && wing[i])) { i++; continue; } let j = i; while (j < n && w[j] && wing[j] && t[j] === t[i]) j++;
      const fromStart = i > 0 && lock[i - 1], fromEnd = j < n && lock[j], cum = [0];
      for (let q = i + 1; q < j; q++) cum.push(cum[q - i - 1] + Math.hypot(S[q].x - S[q - 1].x, S[q].y - S[q - 1].y));
      const tot = cum[cum.length - 1], ramp = tot < 1e-6 ? WALL_RAMP : Math.min(WALL_RAMP, tot);
      const farIdx = fromStart && !fromEnd ? j : i - 1;   // the sample just beyond the far end of the wing
      let base = null; if (farIdx >= 0 && farIdx < n && w[farIdx] && !wing[farIdx] && t[farIdx] === t[i] && pl[farIdx] && !pl[farIdx].full) base = pl[farIdx];
      for (let q = i; q < j; q++) {
        let fw = 1; if (!(fromStart && fromEnd)) { const d = (fromStart ? tot - cum[q - i] : cum[q - i]) + 1e-9, u = Math.min(1, d / ramp); fw = u * u * (3 - 2 * u); }   // fromStart: structure at the low end, far end is high
        pl[q] = { cut: t[q] === 1, n: 0, a: [], z: [], plat: false, nat: false, over: false, full: true, wing: true, fw, base };
      }
      i = j;
    }
  }
  r.overHeight = false;
  for (let i = 0; i < n; i++) { const s = S[i]; s.wL = s.wL0 = typ[0][i]; s.wR = s.wR0 = typ[1][i]; s.pL = plans[0][i]; s.pR = plans[1][i]; s.rockL = !!(s.pL && s.pL.rock); s.rockR = !!(s.pR && s.pR.rock); s.needsL = !!(s.pL && s.pL.needs); s.needsR = !!(s.pR && s.pR.needs); s.sL = sups[0][i] && !s.wL ? 1 : 0; s.sR = sups[1][i] && !s.wR ? 1 : 0; s.oL = !!(s.pL && s.pL.over); s.oR = !!(s.pR && s.pR.over); if (s.oL || s.oR) r.overHeight = true; }
}
function dropShortLen(a, S, minLen, keep) {   // drop true runs whose arc length (plus one nominal step) is under minLen metres, unless a station of the run really needs a wall (keep[i])
  const n = a.length; let i = 0;
  while (i < n) { if (!a[i]) { i++; continue; } let j = i; while (j < n && a[j]) j++;
    let need = false; if (keep) for (let k = i; k < j; k++) need = need || !!keep[k];
    if (!need && S[j - 1].s - S[i].s + SAMPLE_STEP < minLen - 1e-9) for (let k = i; k < j; k++) a[k] = false;
    i = j; }
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
const reachOfRoad = ty => ty.total / 2 + VERGE + (T.wallStart + WALL_SLACK) * T.batter + 2 + ROUND_REACH + DITCH_W;
const endReachOfRoad = ty => ty.total / 2 + VERGE + (T.wallStart + WALL_SLACK) * Math.max(T.batter, END_BATTER) + 2 + ROUND_REACH + DITCH_W;   // run-out beyond a free road end
function stairExtent(r) {   // [lateral reach beyond the footprint edge, run-out beyond a free end] of the terraced sides
  let e = 0, al = 0; const S = r.samples;
  for (let i = 0; i < S.length; i++) for (const p of [S[i].wL && S[i].pL, S[i].wR && S[i].pR]) if (p && p.stairs) { e = Math.max(e, p.reachE); if (i === 0 || i === S.length - 1) al = Math.max(al, p.alongMax); }
  return [e, al];
}
function roadBox(r) {
  const S = r.samples, ty = ROAD_TYPES[r.type], [sE, sA] = stairExtent(r), m = Math.max(reachOfRoad(ty) + WALL_SLOPE_EXTRA, ty.total / 2 + sE + 4) + 1, me = Math.max(endReachOfRoad(ty) + WALL_SLOPE_EXTRA, ty.total / 2 + sE + 4, sA + ty.total / 2 + 4) + 1; let x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (const s of S) { if (s.x < x0) x0 = s.x; if (s.x > x1) x1 = s.x; if (s.y < y0) y0 = s.y; if (s.y > y1) y1 = s.y; }
  const b = { x0: x0 - m, y0: y0 - m, x1: x1 + m, y1: y1 + m };
  for (const s of [S[0], S[S.length - 1]]) { b.x0 = Math.min(b.x0, s.x - me); b.y0 = Math.min(b.y0, s.y - me); b.x1 = Math.max(b.x1, s.x + me); b.y1 = Math.max(b.y1, s.y + me); }
  return b;
}
const unionBox = (a, b) => !a ? { ...b } : { x0: Math.min(a.x0, b.x0), y0: Math.min(a.y0, b.y0), x1: Math.max(a.x1, b.x1), y1: Math.max(a.y1, b.y1) };
// base height at grid texel (i, j): identical to hBase (same triangles) but with integer cell maths
function baseIJ(i, j) {
  const ci = Math.min((i / GK) | 0, TN - 2), cj = Math.min((j / GK) | 0, TN - 2), u = (i - ci * GK) / GK, v = (j - cj * GK) / GK, k = cj * TN + ci;
  const a = TH[k], b = TH[k + 1], c = TH[k + TN], d = TH[k + TN + 1];
  return u >= v ? a + u * (b - a) + v * (d - b) : a + u * (d - c) + v * (c - a);
}
// Squared distance between segments PQ and AB (0 if they cross).
function segSegDist2(px, py, qx, qy, ax, ay, bx, by) {
  const d1x = qx - px, d1y = qy - py, d2x = bx - ax, d2y = by - ay, den = d1x * d2y - d1y * d2x;
  if (Math.abs(den) > 1e-12) { const t = ((ax - px) * d2y - (ay - py) * d2x) / den, u = ((ax - px) * d1y - (ay - py) * d1x) / den; if (t >= 0 && t <= 1 && u >= 0 && u <= 1) return 0; }
  const pt = (x, y, x0, y0, x1, y1) => { const dx = x1 - x0, dy = y1 - y0, L2 = dx * dx + dy * dy; let u = L2 > 1e-12 ? ((x - x0) * dx + (y - y0) * dy) / L2 : 0; u = u < 0 ? 0 : u > 1 ? 1 : u; const ex = x - x0 - u * dx, ey = y - y0 - u * dy; return ex * ex + ey * ey; };
  return Math.min(pt(px, py, ax, ay, bx, by), pt(qx, qy, ax, ay, bx, by), pt(ax, ay, px, py, qx, qy), pt(bx, by, px, py, qx, qy));
}
// Ground value of one wall plan at lateral e (beyond the footprint edge): [kind, v]; kind 0 = nothing (natural ground), 1 = inside a solid (v: road-facing value), 2 = cut surface, 3 = fill surface.
// Tier k occupies e in [w*a[k], w*a[k] + wallT] and levels are zf + w*z[k]; w is the run weight.
function tierEval(pl, w, e, zf, b, res, wallT, bt) {
  const nT = pl.n, cutP = pl.cut; let kk = -1;
  for (let q = nT - 1; q >= 0; q--) if (e >= w * pl.a[q]) { kk = q; break; }
  if (kk < 0) return [cutP ? 2 : 3, zf];
  const tk = w * pl.a[kk], Lp = kk > 0 ? zf + w * pl.z[kk - 1] : zf, Lk = zf + w * pl.z[kk];
  if (e <= tk + wallT - res) return [1, Lp];
  if (e <= tk + wallT) return cutP ? [1, Lk] : [3, Lk];
  if (kk < nT - 1) return [cutP ? 2 : 3, Lk];
  if (pl.plat) return [b > Lk ? 2 : 3, Lk + (b > Lk ? 1 : -1) * (1 - w) * (e - (tk + wallT)) / bt];   // flat up to the neighbour; the slope comes back as the run weight drops (run ends)
  if (pl.nat) {   // the last wall meets the natural ground at its back face / foot (w = 1): the slope beyond it starts at the local natural level, so it rolls into the ground; it starts lower while the run weight is below 1
    const Lf = zf + pl.z[nT - 1], Ls = cutP ? Lk + w * (Math.max(Lf, b - 0.1) - Lf) : Lk + w * (Math.min(Lf, b) - Lf);
    return [cutP ? 2 : 3, Ls + (cutP ? 1 : -1) * (e - (tk + wallT)) / bt];
  }
  return [cutP ? 2 : 3, Lk + (cutP ? 1 : -1) * (e - (tk + wallT)) / bt];
}
// Ground value of a terraced section at lateral e: same convention as tierEval. zf: formation level (lifted beyond a free end); N(e): natural ground along the normal.
function benchZ(cut, t, e, n) { const strip = t.u + (cut ? 1 : -1) * BERM_SLOPE * (e - (t.a + WALL_THICK)); return cut ? Math.max(strip, n - FADE_K * t.eps) : Math.min(strip, n + FADE_K * t.eps); }   // the sloped strip behind a full tier, never carved more than FADE_K * eps below natural ground
function stairEval(pl, e, zf, b, res, wallT, N) {
  if (pl.kind !== 'terrace') return [pl.cut ? 2 : 3, surfZ(pl.kind, pl.cut, e, zf)];   // rock cut / steep fill: a plain surface, natural ground clips it
  const tiers = stairProfile(pl.cut, zf, pl.h, N, pl.mt).tiers, k = Math.floor(e / TIER_PITCH + 1e-12);
  if (k >= tiers.length) return [0, 0];
  const t = tiers[k];
  if (e <= t.a + wallT - res) return [1, t.f];
  if (e <= t.a + wallT) return pl.cut ? [1, t.u] : [3, t.u];
  if (t.eps <= 0) return [0, 0];
  return [pl.cut ? 2 : 3, benchZ(pl.cut, t, e, b)];
}
// A terraced plan as old-style tiers at one station (a[k], z[k] = top relative to the formation), for the wing walls that blend into it.
function stairAsTiers(pl, zf, N) {
  if (pl.kind !== 'terrace') return { n: 0, a: [], z: [], cut: pl.cut };
  const t = stairProfile(pl.cut, zf, pl.h, N, pl.mt).tiers;
  return { n: t.length, a: t.map(q => q.a), z: t.map(q => q.u - zf), cut: pl.cut };
}
// The legacy full wall at weight 1 (retains the natural ground at the texel, whatever its height).
function fullEval(cutP, e, zf, b, res, wallT, verge, bt) {
  if (cutP) { const zfw = Math.max(zf, b - 0.1); return e <= wallT ? [1, e <= wallT - res ? zf : zfw] : [2, zfw + (e - verge) / bt]; }
  if (e <= wallT - res) return [1, zf];
  return [3, e <= wallT ? b : b - (e - verge) / bt];
}
// ---- Pure core (portable to C++): plain arrays and numbers only, no THREE / DOM / globals. ----
// roadsData: [{ hw, reach, bbox:{x0,y0,x1,y1}, samples:[{x,y,z,h,br,tu,wL,wR}] }]; params: { formDrop, batter, wallThick, verge }.
// baseSampler(x, y) -> base height. Writes nx*ny graded heights to out (row-major, texel (i,j) at x0+i*res, y0+j*res).
function gradeTile(baseSampler, roadsData, params, x0, y0, nx, ny, res, out) {
  const N = nx * ny, bt = params.batter, fd = params.formDrop, half = params.wallThick - res;   // wall band: texels with e <= wallThick-res take the inside (formation) value, so the one-texel ramp to natural lies inside the solid
  const verge = params.verge, hb = params.roundHinge || 0, rk = params.roundK || 0, dD = params.ditchD || 0, dW = params.ditchW || 0, eB = params.endBatter || bt, rfL = params.reachFade || 0;
  const sv = e => e <= verge ? 0 : hb > 0 ? (e < verge + hb ? (e - verge) * (e - verge) / (2 * hb) : e - verge - hb / 2) : e - verge;   // verge -> slope hinge: parabola over hb, then the straight slope shifted out by hb/2
  const smax = d => d >= rk ? d : d <= -rk ? 0 : (d + rk) * (d + rk) / (4 * rk);   // smooth max(d, 0) over +-rk: rolls the slope over into natural ground
  const base = new Float32Array(N), fpD = new Float32Array(N).fill(9), fpZ = new Float32Array(N).fill(1e9), cutM = new Float32Array(N).fill(1e9), fillM = new Float32Array(N).fill(-1e9), rqW = new Uint8Array(N), rqP = new Array(N).fill(null), rqBt = new Float32Array(N), rqFw = new Float32Array(N), rqWw = new Float32Array(N), rqD = new Float32Array(N), rqZ = new Float32Array(N), rqEx = new Float32Array(N), rqEy = new Float32Array(N), cutE = new Float32Array(N), fillE = new Float32Array(N), rqAl = new Float32Array(N), rqFs = new Float32Array(N), rqFl = new Uint8Array(N), rqBy = new Uint8Array(N), rqSp = new Float32Array(N), rqSv = new Float32Array(N), cutR = new Float32Array(N).fill(1), fillR = new Float32Array(N).fill(1);
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) base[j * nx + i] = baseSampler(x0 + i * res, y0 + j * res);
  const tx1 = x0 + (nx - 1) * res, ty1 = y0 + (ny - 1) * res;
  for (let ri = 0; ri < roadsData.length; ri++) {
    const rd = roadsData[ri], bb = rd.bbox; if (bb && (bb.x1 < x0 || bb.x0 > tx1 || bb.y1 < y0 || bb.y0 > ty1)) continue;
    const S = rd.samples, hw = rd.hw, R = rd.reach, R2 = R * R, nbr = (rd.nbr || []).map(q => roadsData[q]);
    // a slope never reaches across another road: it is dropped where the straight way from its edge to the texel crosses a neighbour's footprint (so a road that lands on a neighbour's earthworks cannot carve the ground behind it)
    const blocked = (o, i, j, dd) => {
      if (!nbr.length || hw + 0.5 >= dd) return false;
      const tx = x0 + i * res, ty = y0 + j * res, ex = rqEx[o], ey = rqEy[o], f = (hw + 0.5) / dd, qx = tx - ex + ex * f, qy = ty - ey + ey * f;
      for (const nb of nbr) { const bx = nb.core; if (bx && (Math.max(qx, tx) < bx.x0 || Math.min(qx, tx) > bx.x1 || Math.max(qy, ty) < bx.y0 || Math.min(qy, ty) > bx.y1)) continue;
        const Sb = nb.samples, l2 = (nb.hw - 0.25) * (nb.hw - 0.25);
        for (let k = 0; k + 1 < Sb.length; k++) { const a = Sb[k], b = Sb[k + 1]; if ((a.br || a.tu) && (b.br || b.tu)) continue; const m = nb.hw; if (Math.max(a.x, b.x) + m < Math.min(qx, tx) || Math.min(a.x, b.x) - m > Math.max(qx, tx) || Math.max(a.y, b.y) + m < Math.min(qy, ty) || Math.min(a.y, b.y) - m > Math.max(qy, ty)) continue; if (segSegDist2(qx, qy, tx, ty, a.x, a.y, b.x, b.y) < l2) return true; } }
      return false;
    };
    rqD.fill(1e9);   // per road: nearest-station formation; across roads: lowest wins
    for (let k = 0; k + 1 < S.length; k++) {
      const A = S[k], B = S[k + 1];
      if ((A.br || A.tu) && (B.br || B.tu)) continue;   // bridge / tunnel stations are not graded; a segment with one structure end is graded up to the abutment / portal
      const dx = B.x - A.x, dy = B.y - A.y, L2 = dx * dx + dy * dy; if (L2 < 1e-12) continue;
      const Rs = (k === 0 || k === S.length - 2) && rd.endExt > R ? rd.endExt : R;   // beyond a free end the terraces carry on straight (diagonal wall ends)
      const i0 = Math.max(0, Math.floor((Math.min(A.x, B.x) - Rs - x0) / res)), i1 = Math.min(nx - 1, Math.ceil((Math.max(A.x, B.x) + Rs - x0) / res));
      const j0 = Math.max(0, Math.floor((Math.min(A.y, B.y) - Rs - y0) / res)), j1 = Math.min(ny - 1, Math.ceil((Math.max(A.y, B.y) + Rs - y0) / res));
      const dz = B.z - A.z;
      const ap = rd.planes && rd.planes.length ? rd.planes.filter(pl => k >= pl.k0 && k < pl.k1) : null, pc = params.planeC ?? 1e9;
      for (let j = j0; j <= j1; j++) {
        const ry = y0 + j * res - A.y;
        for (let i = i0; i <= i1; i++) {
          const rx = x0 + i * res - A.x;
          if (ap) { const px = rx + A.x, py = ry + A.y; let beyond = false; for (const pl of ap) if ((px - pl.x) * pl.nx + (py - pl.y) * pl.ny > pc) { beyond = true; break; } if (beyond) continue; }   // past a tunnel portal plane: natural ground (the headwall hides the one-texel step)
          let t = (rx * dx + ry * dy) / L2; if ((B.tu && !A.tu && t > 1 + PLANE_OVER / Math.sqrt(L2)) || (A.tu && !B.tu && t < -PLANE_OVER / Math.sqrt(L2))) continue; const tRaw = t;   // tunnel portal: grading stops exactly at the portal plane (the first tunnel station) (the headwall hides the step); bridges keep the rounded embankment end
          t = t < 0 ? 0 : t > 1 ? 1 : t;
          let ex = rx - t * dx, ey = ry - t * dy, d2 = ex * ex + ey * ey, pro = 0, alongV = 0, fs = 0;
          if ((k === 0 && tRaw < 0 && !A.br && !A.tu) || (k === S.length - 2 && tRaw > 1 && !B.br && !B.tu)) {   // beyond a free end: a terraced side carries on straight, its levels rising (cut) / falling (fill) at the run-out batter, so the walls end diagonally into the ground
            const Eq = k === 0 ? A : B, sdL = Eq.pL && Eq.pL.stairs && Eq.wL, sdR = Eq.pR && Eq.pR.stairs && Eq.wR;
            if (sdL || sdR) {
              const sq = Math.sqrt(L2), crs = dx * ry - dy * rx, perp = crs / sq, along = tRaw < 0 ? -(rx * dx + ry * dy) / sq : (rx * dx + ry * dy) / sq - sq;
              const aL = sdL ? Eq.pL.alongMax : 0, aR = sdR ? Eq.pR.alongMax : 0;
              if (Math.abs(perp) <= R && along <= (perp > 0 ? aL : aR) + hw) {
                pro = 1; alongV = along; ex = rx - tRaw * dx; ey = ry - tRaw * dy; d2 = perp * perp;
                if (Math.abs(perp) < hw) { pro = 2; const sL = sdL ? (Eq.pL.cut ? 1 : -1) : 0, sR = sdR ? (Eq.pR.cut ? 1 : -1) : 0, u0 = (perp + hw) / (2 * hw), u1 = Math.min(1, Math.max(0, (u0 - 0.35) / 0.3)), u = u1 * u1 * (3 - 2 * u1); fs = sR + (sL - sR) * u; }
              }
            }
          }
          if (!pro && d2 > R2) continue;
          const dd = Math.sqrt(d2), o = j * nx + i; if (dd >= rqD[o]) continue;   // nearest station of this road decides (one zf, one wall flag per texel)
          rqAl[o] = alongV; rqFl[o] = pro === 2 ? 1 : 0; rqFs[o] = fs; rqBy[o] = !pro && (tRaw < 0 || tRaw > 1) ? (((k === 0 && tRaw < 0 && !A.br && !A.tu) || (k === S.length - 2 && tRaw > 1 && !B.br && !B.tu)) ? 2 : 1) : 0;
          rqD[o] = dd; rqEx[o] = ex; rqEy[o] = ey; rqZ[o] = A.z + t * dz - fd; { let bte = bt; if (eB > bt && dd > 0 && ((k === 0 && tRaw < 0 && !A.br && !A.tu) || (k === S.length - 2 && tRaw > 1 && !B.br && !B.tu))) { const sq = Math.sqrt(L2), along = tRaw < 0 ? -(rx * dx + ry * dy) / sq : (rx * dx + ry * dy) / sq - sq; bte = bt + (eB - bt) * Math.min(1, along / dd); } rqBt[o] = bte; } const E = (A.br || A.tu) ? B : (B.br || B.tu) ? A : t < 0.5 ? A : B; const lf = (dx * ry - dy * rx) > 0; rqW[o] = lf ? E.wL : E.wR; rqSp[o] = lf ? E.sL : E.sR; { const pa = lf ? A.pL : A.pR, pb = lf ? B.pL : B.pR, ya = pa && pa.stairs ? (pa.alt ? pa.sw : 1) : 0, yb = pb && pb.stairs ? (pb.alt ? pb.sw : 1) : 0; rqSv[o] = (A.br || A.tu) ? yb : (B.br || B.tu) ? ya : ya + (yb - ya) * t; } /* the terraces' share beside a terraced run, interpolated between the stations like the other weights */ rqP[o] = lf ? E.pL : E.pR; { const fa = lf ? A.fwL : A.fwR, fb = lf ? B.fwL : B.fwR; rqFw[o] = (A.br || A.tu) ? fb : (B.br || B.tu) ? fa : fa + (fb - fa) * t; } { const wa = (lf ? A.wwL : A.wwR) ?? 1, wb = (lf ? B.wwL : B.wwR) ?? 1; rqWw[o] = (A.br || A.tu) ? wb : (B.br || B.tu) ? wa : wa + (wb - wa) * t; }   // cross > 0: texel on the left
        }
      }
    }
    for (let o = 0; o < N; o++) {
      const dd = rqD[o]; if (dd > 1e8) continue; const zf = rqZ[o];
      if (rqFl[o]) {   // ahead of a free end of a terraced road: the floor rises / falls with the run-out batter (cut / fill side), across the width it is a crossfall between the two
        const lift = rqAl[o] / eB, fs = rqFs[o], zl = zf + fs * lift, b0 = base[o]; let zz = zf; if (fs > 0) zz = Math.min(b0, zl); else if (fs < 0) zz = Math.max(b0, zl);
        fpD[o] = 0; if (zz < fpZ[o]) fpZ[o] = zz; continue; }
      if (dd <= hw) { fpD[o] = 0; if (zf < fpZ[o]) fpZ[o] = zf; continue; }   // union of footprints: lowest formation wins (never pokes through any road)
      if (rqSp[o] >= 1) continue;   // the neighbour's structure reaches this road: no earthworks of its own on this side (they fade back in as the structure tapers)
      const e = dd - hw, b = base[o], cutT = b > zf; let wf = rqW[o], ww = wf ? rqWw[o] : 0, pl = wf ? rqP[o] : null; const bte = rqBt[o];
      const blk = () => blocked(o, o % nx, (o / nx) | 0, dd);   // evaluated only when a slope would be recorded
      const rf = rfL > 0 && dd > R - rfL ? (() => { const u = (R - dd) / rfL; return u <= 0 ? 0 : u * u * (3 - 2 * u); })() : 1;   // a plain slope that has not met the ground by the end of the road's reach fades into it (no cut face / pit at the reach limit)
      const rfs = rf * (1 - rqSp[o]);
      if (wf && pl && pl.stairs && rqBy[o] === 2) continue;   // beyond the run-out of a terraced free end: natural ground (no cone)
      if (wf && pl && pl.stairs && rqBy[o]) { wf = 0; ww = 0; pl = null; }   // terraces do not wrap around a bridge abutment / portal (the wing wall does that): plain run-out slope
      if (wf && !(pl && pl.plat) && (wf === 1) !== cutT) { wf = 0; ww = 0; pl = null; }   // flag says cut but this texel is fill (or vice versa): plain slope
      if (wf && pl && (!pl.full || pl.wing)) {   // stacked walls (see planSide), or a wing wall blended in from the earthworks beside it
        let kind, v;
        const fxs = (x0 + (o % nx) * res) - rqEx[o], fys = (y0 + ((o / nx) | 0) * res) - rqEy[o], uxs = rqEx[o] / dd, uys = rqEy[o] / dd;
        if (pl.stairs) {
          const tx = x0 + (o % nx) * res, ty = y0 + ((o / nx) | 0) * res, fx = tx - rqEx[o], fy = ty - rqEy[o], ux = rqEx[o] / dd, uy = rqEy[o] / dd;
          [kind, v] = stairEval(pl, e, zf + (pl.cut ? 1 : -1) * rqAl[o] / eB, b, res, params.wallThick, q => baseSampler(fx + ux * (hw + q), fy + uy * (hw + q)));
          if (pl.alt && rqSv[o] < 1) {   // beside a terraced run: blend with the station's own plan (one wall and a slope, or a wall to the ground)
            const [ka, va] = tierEval(pl.alt, ww, e, zf, b, res, params.wallThick, bt), eff = (kk, vv) => kk === 1 ? vv : kk === 0 ? b : pl.cut ? Math.min(b, vv) : Math.max(b, vv);
            const ga = eff(ka, va), gs2 = eff(kind, v); kind = pl.cut ? 2 : 3; v = ga + rqSv[o] * (gs2 - ga); }
          if (ww < 1) {   // run ends (where plain slopes carry on): the terraced ground and the plain slope blend by the run weight
            const gs = kind === 1 ? v : kind === 0 ? b : pl.cut ? Math.min(b, v) : Math.max(b, v), gp = pl.cut ? Math.min(b, zf + sv(e) / bt) : Math.max(b, zf - sv(e) / bt);
            const gg = gp + ww * (gs - gp); kind = pl.cut ? 2 : 3; v = gg; }
        } else if (!pl.full) [kind, v] = tierEval(pl, ww, e, zf, b, res, params.wallThick, bt);
        else {   // wing: (1 - fw) * ordinary earthworks (adjacent run's tiers, else the plain slope) + fw * full wall
          let [kb, vb] = pl.base && !(pl.base.stairs && rqBy[o]) ? (pl.base.stairs ? stairEval(pl.base, e, zf, b, res, params.wallThick, q => baseSampler(fxs + uxs * (hw + q), fys + uys * (hw + q))) : tierEval(pl.base, ww, e, zf, b, res, params.wallThick, bt)) : [pl.cut ? 2 : 3, pl.cut ? zf + sv(e) / bt : zf - sv(e) / bt];
          const [kf, vf] = fullEval(pl.cut, e, zf, b, res, params.wallThick, verge, bt);
          if (kb === 0) { kb = kf; vb = pl.cut ? b + rk : b - rk; }   // natural ground carries on beside the adjacent wall: nothing to carve
          kind = kb === 1 || kf === 1 ? 1 : kf; const vfe = kb === 1 && kf !== 1 ? (pl.cut ? Math.min(vf, b) : Math.max(vf, b)) : vf; v = vb + rqFw[o] * (vfe - vb);   // a solid of the adjacent terraces beside the wing: where the full wall has only a slope, the ground there is natural, not the slope level
        }
        if (kind === 0) continue;
        if (kind === 1) { fpD[o] = 0; const zw = pl.cut && !pl.plat ? Math.min(v, b) : pl.stairs && !pl.cut ? Math.max(v, b) : v; /* a shared structure (plat) builds the ground up to the neighbour whatever the natural ground does */ if (zw < fpZ[o]) fpZ[o] = zw; continue; }
        const rr = pl.stairs && pl.kind !== 'terrace' ? rfs * (() => { const u = rqAl[o] / (pl.alongMax + 1e-9); return u <= 0 ? 1 : u >= 1 ? 0 : 1 - u * u * (3 - 2 * u); })() : 1;   // also fades out along the run-out beyond a free end   // rock cut / steep fill fade into the ground at the end of the reach like plain slopes
        if (kind === 2) { if (rr > 0 && v < b + rk && v < cutM[o] && !blk()) { cutM[o] = v; cutE[o] = e; cutR[o] = rr; } }
        else if (rr > 0 && v > b - rk && v > fillM[o] && !blk()) { fillM[o] = v; fillE[o] = e; fillR[o] = rr; }
        continue; }
      // Wall (ww = weight 0..1 along the run; verge == wall thickness so ww = 0 is exactly the plain verge + slope).
      // cut wall: the wall raises the formation level toward natural by ww (hidden behind the road-facing wall face), the slope starts above it.
      // fill wall: the wall's foot sits ww of the way from formation down to natural; the slope continues below the foot.
      if (wf === 1) {
        const zfw = zf + ww * (Math.max(zf, b - 0.1) - zf);
        if (e <= params.wallThick) { fpD[o] = 0; const zw = e <= half ? Math.min(zf, b) : Math.min(zfw, b); if (zw < fpZ[o]) fpZ[o] = zw; continue; }
        const v = zfw + (e - verge) / bt; if (rfs > 0 && v < b + rk && v < cutM[o] && !blk()) { cutM[o] = v; cutE[o] = e; cutR[o] = rfs; }
        continue; }
      if (wf === 2) {
        if (e <= half) { fpD[o] = 0; if (zf < fpZ[o]) fpZ[o] = zf; continue; }
        const zff = zf - ww * (zf - b), v = e <= params.wallThick ? zff : zff - (e - verge) / bt; if (rfs > 0 && v > b - rk && v > fillM[o] && !blk()) { fillM[o] = v; fillE[o] = e; fillR[o] = rfs; }
        continue; }
      if (cutT) {   // toe ditch between verge and slope (fades out in shallow cuts), then the slope
        let v = zf + sv(e) / bt; if (dD > 0 && dW > 0 && e > verge && e < verge + dW) { const u = Math.sin(Math.PI * (e - verge) / dW); v -= dD * Math.min(1, b - zf) * u * u; }
        if (rfs > 0 && v < b + rk && v < cutM[o] && !blk()) { cutM[o] = v; cutE[o] = e; cutR[o] = rfs; } }
      else { const v = zf - sv(e) / bte; if (rfs > 0 && v > b - rk && v > fillM[o] && !blk()) { fillM[o] = v; fillE[o] = e; fillR[o] = rfs; } }
    }
  }
  for (let o = 0; o < N; o++) {
    let g;
    if (fpD[o] < 9) g = fpZ[o]; else { g = base[o]; const dc = cutM[o] < 1e8 ? (rk > 0 ? smax(g - cutM[o]) : Math.max(0, g - cutM[o])) * cutR[o] : 0, df = fillM[o] > -1e8 ? (rk > 0 ? smax(fillM[o] - g) : Math.max(0, fillM[o] - g)) * fillR[o] : 0;   // dip below natural (cut) / rise above it (fill), rounded near daylight
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
  const hw = ty.total / 2, cap = reachOfRoad(ty), capE = endReachOfRoad(ty), bt = T.batter; let mx = 0, tiers = false, stairE = 0;
  const S = r.samples;
  for (let i = 0; i < S.length; i += 2) { const s = S[i]; if (s.br || s.tu) continue;
    const zf = s.z - FORM_DROP;
    const endish = i === 0 || i + 2 >= S.length - 1 || (S[i - 2] && (S[i - 2].br || S[i - 2].tu)) || (S[i + 2] && (S[i + 2].br || S[i + 2].tu));   // road end or next to a bridge / tunnel: the embankment also falls away along the road (cone around the end)
    for (const sg of endish ? [1, -1, 2, 3] : [1, -1]) { if (sg === 1 ? s.wL : sg === -1 ? s.wR : false) { const p = sg === 1 ? s.pL : s.pR; if (p && !p.full) tiers = true; if (p && p.stairs) stairE = Math.max(stairE, p.reachE); continue; }
      const nx = sg > 1 ? Math.cos(s.h) * (sg === 2 ? 1 : -1) : -Math.sin(s.h) * sg, ny = sg > 1 ? Math.sin(s.h) * (sg === 2 ? 1 : -1) : Math.cos(s.h) * sg;
      const bd = sg > 1 && !(i === 0 || i + 2 >= S.length - 1) ? bt : sg > 1 ? Math.max(bt, END_BATTER) : bt, cp = sg > 1 ? capE : cap;   // run-out batter only beyond a free road end (not beside a bridge / tunnel)
      for (let e = 0.5; hw + e <= cp; e += 1) { const b = hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), v = b > zf ? zf + slopeV(e) / bd : zf - slopeV(e) / bd; if (b > zf ? v >= b : v <= b) { if (e > mx) mx = e; break; } if (hw + e + 1 > cp && e > mx) mx = e; } } }
  const structs = S.some(q => q.br || q.tu);
  r._reach = structs || tiers ? Math.max(cap + (tiers ? WALL_SLOPE_EXTRA : 0), Math.min(capE, hw + mx + 2.5 + ROUND_REACH + DITCH_W)) : Math.min(capE, hw + mx + 2.5 + ROUND_REACH + DITCH_W); /* embankment ends at bridges fall away along the road too: full reach */
  if (stairE > 0) r._reach = Math.max(r._reach, hw + stairE + 4);   // terraced sides reach as far as their last tier
  return r._reach;
}
// Wall weight per sample and side (wwL / wwR in 0..1): 0 at either end of a wall run, 1 further than WALL_RAMP m inside it. The ground OUTSIDE a wall's
// outer face blends from the 1:batter slope (weight 0) to natural ground (weight 1), so a wall end never leaves a bare step beside it. Runs and
// medians are final here (markMedians has already cleared median sides).
function wallWeights(S) {
  const n = S.length;
  for (const [fl, key, pk] of [['wL', 'wwL', 'pL'], ['wR', 'wwR', 'pR']]) {
    let i = 0;
    while (i < n) { if (!S[i][fl]) { S[i][key] = 0; i++; continue; } let j = i; while (j < n && S[j][fl]) j++;
      const cum = [0]; for (let k = i + 1; k < j; k++) cum.push(cum[k - i - 1] + Math.hypot(S[k].x - S[k - 1].x, S[k].y - S[k - 1].y));
      const tot = cum[cum.length - 1];
      for (let k = i; k < j; k++) { const stairsRun = !!(S[i][pk] && S[i][pk].stairs), free0 = (i === 0 && !stairsRun) || (i > 0 && !S[i - 1].br && !S[i - 1].tu), free1 = (j === n && !stairsRun) || (j < n && !S[j].br && !S[j].tu);   // a terraced run keeps full weight up to the road's own end (its walls end diagonally beyond it)   // only free run ends (incl. the road's own ends) taper: structures (abutment, portal) hide the wall end
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
    r.bbox = roadBox(r);
  }
  markMedians();
  for (const r of rs) wallWeights(r.samples);
  // the upper road of a shared structure builds nothing on that side while the lower road's structure is at full weight (its own earthworks come back as the structure tapers at the ends)
  for (const r of rs) for (const s of r.samples) { s.supL = s.supR = 0; if (!(s.sL || s.sR)) continue;
    let best = 1e18, w = 0; for (const o of rs) { if (o === r || o.bbox.x1 < r.bbox.x0 || o.bbox.x0 > r.bbox.x1 || o.bbox.y1 < r.bbox.y0 || o.bbox.y0 > r.bbox.y1) continue;
      for (const q of o.samples) { const pl = q.wL && q.pL && q.pL.plat ? 1 : q.wR && q.pR && q.pR.plat ? 2 : 0; if (!pl) continue; const d = (q.x - s.x) ** 2 + (q.y - s.y) ** 2; if (d < best) { best = d; w = pl === 1 ? q.wwL : q.wwR; } } }
    if (best < 1e17) { if (s.sL) s.supL = w; if (s.sR) s.supR = w; } }
  const rdOut = roads.filter(r => r.samples && r.samples.length > 1).map(r => { const ty = ROAD_TYPES[r.type]; const R = slopeReach(r, ty), sb = [], full = r.samples, dec = [];
    const pk = a => ({ x: a.x, y: a.y, z: a.z, h: a.h, wL: a.wL, wR: a.wR, sL: a.supL || 0, sR: a.supR || 0, pL: a.pL, pR: a.pR, fwL: a.pL && a.pL.wing ? a.pL.fw : 0, fwR: a.pR && a.pR.wing ? a.pR.fw : 0, wwL: a.wwL, wwR: a.wwR, br: a.br, tu: a.tu, mL: a.mL, mR: a.mR });
    for (let i = 0; i < full.length; i++) { const a = full[i]; if (i % 2 === 0 || i === full.length - 1 || !!full[i - 1].tu !== !!a.tu || !!full[i + 1].tu !== !!a.tu || !!full[i - 1].br !== !!a.br || !!full[i + 1].br !== !!a.br) dec.push(pk(a)); }   // every 2nd sample, plus the last graded / first tunnel sample at each portal (the portal plane stays exact)   // 4 m chords (sag < 0.1 m at minR 20) halve the rasterising cost
    for (let i = 0; i + 1 < dec.length; i += 2) { const a = dec[i], b = dec[Math.min(i + 2, dec.length - 1)]; sb.push([Math.min(a.x, b.x) - R, Math.min(a.y, b.y) - R, Math.max(a.x, b.x) + R, Math.max(a.y, b.y) + R]); }   // coarse reach boxes for tile culling
    const planes = [];   // portal planes: decimated-segment range they cut, plane point, unit normal pointing into the tunnel
    const plen = (a, b) => { let d = 0; for (let q = a; q < b; q++) d += Math.hypot(dec[q + 1].x - dec[q].x, dec[q + 1].y - dec[q].y); return d; }, span = R + PLANE_MARGIN;
    for (let k = 0; k < dec.length; k++) { const a = dec[k], pr = dec[k - 1], nx_ = dec[k + 1]; if (!a.tu) continue;
      if (pr && !pr.tu) { let k0 = k; while (k0 > 0 && plen(k0, k) < span) k0--; planes.push({ k0, k1: k, x: a.x, y: a.y, nx: Math.cos(a.h), ny: Math.sin(a.h) }); }
      if (nx_ && !nx_.tu) { let k1 = k; while (k1 < dec.length - 1 && plen(k, k1) < span) k1++; planes.push({ k0: k, k1, x: a.x, y: a.y, nx: -Math.cos(a.h), ny: -Math.sin(a.h) }); } }
    const endExt = (() => { let m = 0; for (const q of [full[0], full[full.length - 1]]) for (const pp of [q.wL && q.pL, q.wR && q.pR]) if (pp && pp.stairs) m = Math.max(m, pp.alongMax + ty.total / 2 + 2); return m; })();
    return { hw: ty.total / 2, reach: R, endExt, bbox: r.bbox, samples: dec, segBoxes: sb, planes, nbr: [], core: { x0: Math.min(...r.samples.map(q => q.x)) - ty.total / 2, x1: Math.max(...r.samples.map(q => q.x)) + ty.total / 2, y0: Math.min(...r.samples.map(q => q.y)) - ty.total / 2, y1: Math.max(...r.samples.map(q => q.y)) + ty.total / 2 } }; });
  rdOut.forEach((a, i) => { a.nbr = []; rdOut.forEach((b, j) => { if (i !== j && !(b.bbox.x1 < a.bbox.x0 || b.bbox.x0 > a.bbox.x1 || b.bbox.y1 < a.bbox.y0 || b.bbox.y0 > a.bbox.y1)) a.nbr.push(j); }); });   // roads whose reach boxes overlap: a slope may not reach across them
  return rdOut.concat(typeof inspectFootprints === 'function' ? inspectFootprints() : []);   // inspect.js junction surfaces
}
// Recompute GH inside box {x0,y0,x1,y1} (world metres) from base + all roads. Calls onGroundChanged(box) with the snapped box.
function gradeRegion(box) {
  const gi0 = clamp(Math.floor(box.x0 / GRID_RES), 0, GN - 1), gj0 = clamp(Math.floor(box.y0 / GRID_RES), 0, GN - 1);
  const gi1 = clamp(Math.ceil(box.x1 / GRID_RES), 0, GN - 1), gj1 = clamp(Math.ceil(box.y1 / GRID_RES), 0, GN - 1);
  if (gi1 < gi0 || gj1 < gj0) return;
  const rd = roadsData(), TS = TILE_M / GRID_RES, params = { formDrop: FORM_DROP, batter: T.batter, wallThick: WALL_THICK, verge: VERGE, planeC: PLANE_OVER, roundK: ROUND_DAYLIGHT / (2 * T.batter), roundHinge: ROUND_HINGE, ditchD: DITCH_D, ditchW: DITCH_W, endBatter: END_BATTER, reachFade: REACH_FADE };
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
function wallRuns(r) {   // one run per side and tier; the tier's lateral offset is baked into the points (lo = 0, hi = WALL_THICK from the point). Heights follow the run weight: zero at free ends.
  const S = r.samples, hw = ROAD_TYPES[r.type].total / 2, runs = [];
  if (!S) return runs;
  for (const side of ['L', 'R']) {
    const sg = side === 'L' ? 1 : -1;
    const wFlag = q => q && (side === 'L' ? q.wL : q.wR) && !q.br && !q.tu;
    // the grading interpolates weights linearly between its decimated stations (every 2nd sample): do the same, so wall heights meet the graded ground
    const val = (q, key) => key === 'fwL' ? (q.pL && q.pL.wing ? q.pL.fw : 0) : key === 'fwR' ? (q.pR && q.pR.wing ? q.pR.fw : 0) : q[key];
    const chord = (i, ref, key) => { if (i % 2 === 0 || i === S.length - 1 || ref !== S[i]) return val(ref, key); const a = S[i - 1], b = S[Math.min(i + 1, S.length - 1)]; return wFlag(a) && wFlag(b) ? (val(a, key) + val(b, key)) / 2 : val(ref, key); };
    for (let k = 0; k < MAX_TIERS; k++) {
      let cur = null;
      for (let i = 0; i < S.length; i++) {
        const s = S[i];
        const ref = wFlag(s) ? s : (s.tu && s.portal && wFlag(S[i - 1])) ? S[i - 1] : (s.tu && s.portal && wFlag(S[i + 1])) ? S[i + 1] : null;   // a portal station joins the wall run beside it: the approach wall runs right up to the headwall plane
        if (!ref) { cur = null; continue; }
        const pref = (ref === s && i % 2 === 1 && i + 1 < S.length && wFlag(S[i + 1])) ? S[i + 1] : ref;   // the grading takes the plan of the decimated station at or after an odd sample (its t >= 0.5 rule)
        const p = side === 'L' ? pref.pL : pref.pR, wk = side === 'L' ? 'wwL' : 'wwR', w = chord(i, ref, wk) ?? 1, nx = -Math.sin(s.h) * sg, ny = Math.cos(s.h) * sg, zf = s.z - FORM_DROP;
        const wing = !!(p && p.wing), tp = wing ? (p.base && p.base.stairs ? stairAsTiers(p.base, zf, e => hBase(s.x + nx * (hw + e), s.y + ny * (hw + e))) : p.base) : p && !p.full ? p : null, fw = wing ? chord(i, ref, side === 'L' ? 'fwL' : 'fwR') : 0;   // tp: the tiers in play (own plan, or the adjacent run's beside a wing)
        if (p && p.stairs && p.kind !== 'terrace' && !(p.alt && k === 0)) { cur = null; continue; }   // rock cut / steep fill: no walls (only the neighbouring station's own wall fading out)
        if (p && p.stairs) {   // terraced section: the walls come from the same profile the grading evaluates
          const stN = (q, ox, oy) => e => hBase(ox + nx * (hw + e), oy + ny * (hw + e)), sgT = p.cut ? 1 : -1;
          const swOf = q => { const pq = side === 'L' ? q.pL : q.pR; return pq && pq.stairs ? (pq.alt ? pq.sw : 1) : 0; };
          const wwo = chord(i, ref, wk) ?? 1, sw = (i % 2 === 1 && i + 1 < S.length && wFlag(S[i - 1]) && wFlag(S[i + 1])) ? (swOf(S[i - 1]) + swOf(S[i + 1])) / 2 : (p.alt ? p.sw : 1), tierPt = (ox, oy, zfq) => {
            const Nn = e => hBase(ox + nx * (hw + e), oy + ny * (hw + e));
            let t = stairProfile(p.cut, zfq, p.h, Nn, p.mt).tiers[k];
            const hasAlt = p.alt && k === 0 && p.alt.n > 0; if (!t && hasAlt) t = { a: 0, f: zfq, u: zfq, eps: 0 };
            if (!t) return null;
            // the ground the grading makes at the wall's front / back face: terraces, blended beside a terraced run with the station's own plan (sw) and with the plain slope at run ends (ww)
            const gAt = (e, gs, back) => { let g = gs;
              if (sw < 1) { let ga = Nn(e); if (p.alt) { const [ka, va] = tierEval(p.alt, wwo, e, zfq, ga, GRID_RES, WALL_THICK, T.batter); ga = ka === 1 ? va : ka === 0 ? ga : p.cut ? Math.min(ga, va) : Math.max(ga, va); } g = ga + sw * (g - ga); }
              if (wwo < 1) { const b = Nn(e), gp = p.cut ? Math.min(b, zfq + svTop(e) / T.batter) : Math.max(b, zfq - svTop(e) / T.batter); g = gp + wwo * (g - gp); }
              return g; };
            const gB = gAt(t.a + WALL_THICK, t.u, true), gF = gAt(t.a, t.f, false);
            if (Math.abs(gB - gF) < WALL_VIS_MIN) return null;
            { const hStruct = hasAlt ? (1 - sw) * wwo * Math.abs(p.alt.z[0]) + sw * wwo * Math.abs(t.u - t.f) : sw * wwo * Math.abs(t.u - t.f); if (hStruct < WALL_VIS_MIN) return null; }
            // the ground just behind the back face (ground the wall must still hide: natural where it carries on, else the bench)
            const beyond = d => { const eBe = t.a + WALL_THICK + d, nBe = Nn(eBe); return gAt(eBe, t.eps <= 0 ? nBe : benchZ(p.cut, t, eBe, nBe), true); }, gBeyond = Math.max(beyond(0.5), beyond(0.25) + 0.3);
            // fill: the bench of the tier above, just in front of the wall (it follows the natural ground up to the front level)
            let gFront = gF; if (!p.cut && k > 0) { const tp0 = stairProfile(p.cut, zfq, p.h, Nn, p.mt).tiers[k - 1]; gFront = Math.max(gAt(t.a - 0.1, tp0.u, false), tp0.u); }
            return { x: ox + nx * (hw + t.a), y: oy + ny * (hw + t.a), zTop: p.cut ? gB : gF, zBot: p.cut ? gF : gB, top: p.cut ? Math.max(gB + 0.35, gBeyond + 0.02) : k === 0 ? s.z + 1.0 : Math.max(gF + 0.4, gFront + 0.32), h: s.h, nat: t.eps <= 0 }; };
          const main = tierPt(s.x, s.y, zf);
          const ext = [];   // beyond a free road end the wall carries on straight while it still retains something (its levels rise / fall at the run-out batter)
          if ((i === 0 || i === S.length - 1) && main) for (let a = SAMPLE_STEP; a <= p.alongMax + hw + 2; a += SAMPLE_STEP) { const dirx = Math.cos(s.h) * (i === 0 ? -1 : 1), diry = Math.sin(s.h) * (i === 0 ? -1 : 1), e1 = tierPt(s.x + dirx * a, s.y + diry * a, zf + sgT * a / END_BATTER); if (!e1) break; ext.push(e1); }
          if (!main) { cur = null; continue; }
          if (!cur || cur.cut !== p.cut) { cur = { side, cut: p.cut, lo: 0, hi: WALL_THICK, tier: k, over: false, pts: [] }; runs.push(cur); }
          if (p.over) cur.over = true;
          if (i === 0) for (let q = ext.length - 1; q >= 0; q--) cur.pts.push(ext[q]);
          cur.pts.push(main);
          if (i !== 0) for (const e1 of ext) cur.pts.push(e1);
          continue;
        }
        let cut, off = 0, zTop, zBot, top;
        const out = hBase(s.x + nx * (hw + WALL_THICK), s.y + ny * (hw + WALL_THICK)); cut = p ? p.cut : out > zf;
        const sgn = cut ? 1 : -1;
        if (p && !p.full) {
          if (k >= p.n || Math.abs(p.z[k] - (k ? p.z[k - 1] : 0)) < 1e-9) { cur = null; continue; }
          off = w * p.a[k]; const Lp = zf + w * (k ? p.z[k - 1] : 0), Lk = zf + w * p.z[k];
          if (cut) { zTop = Lk; zBot = Lp; top = Lk + 0.4 + Math.max(0, 0.5 - (Lk - Lp)); } else { zTop = Lp; zBot = Lk; top = k === 0 ? zf + 0.3 + w * (s.z + 1.0 - zf - 0.3) : Lp + 0.4; }
        } else if (k > 0 && wing && p.base && p.base.stairs && p.base.kind === 'terrace') {   // wing beside terraces: tiers k >= 1 are (1 - fw) of the terraces and fw of the natural ground (what the grading makes outside the full wall)
          const Nn = e => hBase(s.x + nx * (hw + e), s.y + ny * (hw + e)), tiersW = stairProfile(p.cut, zf, p.base.h, Nn, p.base.mt).tiers, t = tiersW[k];
          if (!t) { cur = null; continue; }
          const gB = (1 - fw) * t.u + fw * Nn(t.a + WALL_THICK), gF = (1 - fw) * t.f + fw * Nn(t.a);
          if (Math.abs(gB - gF) < WALL_VIS_MIN || (1 - fw) * Math.abs(t.u - t.f) < WALL_VIS_MIN) { cur = null; continue; }   // the structure part (not just the natural slope across the band) must be a wall
          off = t.a; zTop = cut ? gB : gF; zBot = cut ? gF : gB;
          const tq = tiersW[k - 1], gFr = (1 - fw) * tq.u + fw * Nn(t.a - 0.1);   // the bench of the tier above (at most its top), just in front
          const eBe = t.a + WALL_THICK + 0.5, nBe = Nn(eBe), gBe = (1 - fw) * (t.eps <= 0 ? nBe : benchZ(cut, t, eBe, nBe)) + fw * nBe;
          top = cut ? Math.max(gB + 0.35, gBe + 0.02) : Math.max(gF + 0.4, gFr + 0.32);
        } else {
          if (k > 0 && !(wing && tp && k < tp.n && Math.abs(tp.z[k] - tp.z[k - 1]) > 1e-9)) { cur = null; continue; }
          const H = cut ? Math.max(out - zf, 0) : Math.max(zf - out, 0), wf = wing ? fw : w;   // legacy full wall; a wing blends (1 - fw) of the adjacent tiers with fw of the full wall
          const H0 = wing ? (1 - fw) * (tp && tp.n > 0 ? w * Math.abs(tp.z[0]) : 0) + fw * H : w * H, L0 = zf + sgn * H0;
          if (k === 0) { if (cut) { zTop = L0; zBot = zf; top = L0 + 0.4 + Math.max(0, 0.5 - (L0 - zf)); } else { zTop = zf; zBot = L0; top = zf + 0.3 + wf * (s.z + 1.0 - zf - 0.3); } }
          else { off = w * tp.a[k]; const Lp = L0 + (1 - fw) * w * (tp.z[k - 1] - tp.z[0]), Lk = Lp + (1 - fw) * w * (tp.z[k] - tp.z[k - 1]); if (cut) { zTop = Lk; zBot = Lp; top = Lk + 0.4 + Math.max(0, 0.5 - (Lk - Lp)); } else { zTop = Lp; zBot = Lk; top = Lp + 0.4; } }
        }
        const nat = !!(p && (p.full ? !p.wing : p.nat && k === p.n - 1));   // natural ground carries on beyond this wall's outer face
        if (!cur || cur.cut !== cut) { cur = { side, cut, lo: 0, hi: WALL_THICK, tier: k, over: false, pts: [] }; runs.push(cur); }
        if (p && p.over) cur.over = true;
        cur.pts.push({ x: s.x + nx * (hw + off), y: s.y + ny * (hw + off), zTop, zBot, top, h: s.h, nat });
      }
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
