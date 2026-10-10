// roadmesh.js: materials, mesh helpers and buildRoadGroup (surface, markings, skirts, walls, bridges, tunnels).
// ===== Mesh builder: flat-shaded quads in world coords; each quad is wound to face a hint direction =====
class MB {
  constructor(col) { this.p = []; this.n = []; this.c = []; this.i = []; this.def = col; }
  quad(a, b, c, d, hint, col) {
    const ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = d[0] - a[0], vy = d[1] - a[1], vz = d[2] - a[2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    let P = [a, b, c, d], C = col && col.length === 4 ? [col[0], col[1], col[2], col[3]] : null;
    if (nx * hint[0] + ny * hint[1] + nz * hint[2] < 0) { P = [a, d, c, b]; if (C) C = [C[0], C[3], C[2], C[1]]; nx = -nx; ny = -ny; nz = -nz; }
    const l = Math.hypot(nx, ny, nz) || 1; nx /= l; ny /= l; nz /= l;
    const base = this.p.length / 3, dc = col && col.length === 3 ? col : this.def;
    for (let q = 0; q < 4; q++) {
      const w = w3(P[q][0], P[q][1], P[q][2]), cc = C ? C[q] : dc;
      this.p.push(w[0], w[1], w[2]); this.n.push(nx, nz, -ny); this.c.push(cc[0], cc[1], cc[2]);
    }
    this.i.push(base, base + 1, base + 2, base, base + 2, base + 3);
  }
  mesh(mat, shadow) {
    if (!this.i.length) return null;
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.p, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.n, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.c, 3));
    if (mat.map) { const uv = new Float32Array(this.p.length / 3 * 2); for (let q = 0; q < uv.length / 2; q++) { uv[q * 2] = this.p[q * 3] * 0.25; uv[q * 2 + 1] = this.p[q * 3 + 2] * 0.25; } g.setAttribute('uv', new THREE.BufferAttribute(uv, 2)); }
    g.setIndex(this.i);
    const m = new THREE.Mesh(g, mat); m.receiveShadow = true; m.castShadow = !!shadow; return m;
  }
}
const lin = hex => { const c = new THREE.Color(hex); return [c.r, c.g, c.b]; };
const COL = { asph: lin('#3c3f44'), pave: lin('#a5a49d'), kerb: lin('#c3c2bb'), verge: lin('#587a3a'), soil: lin('#8a8658'), mark: lin('#f2f2f2'), conc: lin('#b4b2ab'), concDk: lin('#8f8d87'), cap: lin('#628443'), tube: lin('#2a2a2d'), parapet: lin('#c9c7c0') };
const stdMat = (o = {}) => new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.92, metalness: 0, ...o });
// Subtle procedural asphalt grain (near-white so vertex colours still drive the tint); uv = world metres / 4.
const asphTex = (() => {
  try {
    const cv = document.createElement('canvas'); cv.width = cv.height = 256; const x = cv.getContext('2d'), im = x.createImageData(256, 256);
    for (let i = 0; i < 256 * 256; i++) { const v = 205 + Math.random() * 50 | 0, k = Math.random() < 0.03 ? -35 : 0; im.data[i * 4] = im.data[i * 4 + 1] = im.data[i * 4 + 2] = v + k; im.data[i * 4 + 3] = 255; }
    x.putImageData(im, 0, 0);
    for (let i = 0; i < 40; i++) { x.fillStyle = `rgba(0,0,0,${0.03 + Math.random() * 0.04})`; x.fillRect(Math.random() * 256, Math.random() * 256, 8 + Math.random() * 40, 2 + Math.random() * 6); }
    const t = new THREE.CanvasTexture(cv); t.wrapS = t.wrapT = THREE.RepeatWrapping; t.colorSpace = THREE.SRGBColorSpace; t.anisotropy = 4; return t;
  } catch (e) { return null; }
})();
const MAT = {
  asph: stdMat({ roughness: 0.9, map: asphTex }), pave: stdMat(), kerb: stdMat(), verge: stdMat({ roughness: 1 }), soil: stdMat({ roughness: 1 }),
  mark: stdMat({ roughness: 0.7, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4 }),
  conc: stdMat({ roughness: 0.85 }), cap: stdMat({ roughness: 1 }), tube: stdMat({ roughness: 0.9, emissive: 0x141416 }),
};
// Per-road material variants: higher road id => pulled slightly closer in depth, so overlaps never z-fight; markings always above every surface.
const roadMatCache = new Map();
function roadMats(id) {
  const q = id % 64; let m = roadMatCache.get(q);
  if (!m) {
    const po = (mat, f, u) => { const c = mat.clone(); c.polygonOffset = true; c.polygonOffsetFactor = f; c.polygonOffsetUnits = u; return c; };
    m = { asph: po(MAT.asph, -q * 0.1, -q), pave: po(MAT.pave, -q * 0.1 - 0.05, -q - 0.5), verge: po(MAT.verge, -q * 0.1, -q), kerb: po(MAT.kerb, -q * 0.1 - 0.05, -q - 0.5), mark: po(MAT.mark, -10 - q * 0.1, -100 - q) };
    roadMatCache.set(q, m);
  }
  return m;
}
const HEAT = { '#e5484d': lin('#e5484d'), '#f5a623': lin('#f5a623'), '#e8d44d': lin('#e8d44d'), '#3fb950': lin('#3fb950') };
function heatCurv(k, ty) { const R = Math.abs(k) > 1e-9 ? 1 / Math.abs(k) : 1e9; return R < ty.minR * 0.5 ? '#e5484d' : R < ty.minR ? '#f5a623' : R < ty.minR * 2 ? '#e8d44d' : '#3fb950'; }
function heatGrade(g, ty) { const p = Math.abs(g) * 100; return p > ty.maxGrade * 1.5 ? '#e5484d' : p > ty.maxGrade ? '#f5a623' : p > ty.maxGrade * 0.5 ? '#e8d44d' : '#3fb950'; }
const UP = [0, 0, 1], DOWN = [0, 0, -1];
const TCUT = 8.0, TFILL = 4.8;   // wall block thickness (cut: behind the wall line; fill: under the road edge)

// Interpolated pose on a road at arc length s.
function poseAt(r, s) {
  const S = r.samples; let lo = 0, hi = S.length - 1;
  s = clamp(s, S[0].s, S[hi].s);
  while (hi - lo > 1) { const m = (lo + hi) >> 1; if (S[m].s <= s) lo = m; else hi = m; }
  const a = S[lo], b = S[hi], t = b.s > a.s ? (s - a.s) / (b.s - a.s) : 0;
  return { x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, z: a.z + (b.z - a.z) * t, h: a.h + (b.h - a.h) * t };
}
const latPt = (p, lat, z) => [p.x - Math.sin(p.h) * lat, p.y + Math.cos(p.h) * lat, z];

function buildRoadGroup(r) {
  const ty = ROAD_TYPES[r.type], S = r.samples, n = S.length, hw = ty.total / 2, cw = ty.carriageway / 2;
  const zo = (r.id % 64) * 0.0025, g = new THREE.Group(), RM = roadMats(r.id);
  const B = {}; for (const k of Object.keys(COL)) B[k] = new MB(COL[k]);
  B.tunRoad = new MB(COL.asph);
  const P = (k, lat, dz = 0) => latPt(S[k], lat, S[k].z + zo + dz);
  const tunSeg = k => S[k].tu && S[k + 1].tu;
  const lnorm = h => [-Math.sin(h), Math.cos(h), 0], fwd = h => [Math.cos(h), Math.sin(h), 0];
  const neg = v => [-v[0], -v[1], -v[2]];
  const strip = (bk, l0, l1, dz, hint, filt) => { for (let k = 0; k + 1 < n; k++) if (!filt || filt(k)) bk.quad(P(k, l0, dz), P(k + 1, l0, dz), P(k + 1, l1, dz), P(k, l1, dz), hint); };
  const face = (bk, lat, z0, z1, side, filt) => { // vertical strip along the road at lateral offset
    for (let k = 0; k + 1 < n; k++) if (!filt || filt(k)) bk.quad(P(k, lat, z0), P(k + 1, lat, z0), P(k + 1, lat, z1), P(k, lat, z1), [-Math.sin(S[k].h) * side, Math.cos(S[k].h) * side, 0]);
  };
  // Generic extruded box along samples a..b (inclusive); zb/zt are absolute-height functions of the sample index.
  const extrude = (a, b, l0, l1, zb, zt, o = {}) => {
    const tb = o.top || B.conc, sb = o.side || B.conc;
    for (let k = a; k < b; k++) {
      const q = (l, f, kk) => latPt(S[kk], l, f(kk));
      if (!o.noTop) tb.quad(q(l0, zt, k), q(l0, zt, k + 1), q(l1, zt, k + 1), q(l1, zt, k), UP);
      if (!o.noL1) sb.quad(q(l1, zb, k), q(l1, zb, k + 1), q(l1, zt, k + 1), q(l1, zt, k), lnorm(S[k].h));
      if (!o.noL0) sb.quad(q(l0, zb, k), q(l0, zb, k + 1), q(l0, zt, k + 1), q(l0, zt, k), neg(lnorm(S[k].h)));
      if (o.bottom) sb.quad(q(l0, zb, k), q(l0, zb, k + 1), q(l1, zb, k + 1), q(l1, zb, k), DOWN);
    }
    for (const [kk, sg] of [[a, -1], [b, 1]]) {
      const q = (l, f) => latPt(S[kk], l, f(kk)), hn = fwd(S[kk].h).map(v => v * sg);
      sb.quad(q(l0, zb), q(l1, zb), q(l1, zt), q(l0, zt), hn);
    }
  };
  // Oriented box at sample k (along / lateral / vertical ranges).
  const obox = (k, a0, a1, l0, l1, z0, z1, bk, topB) => {
    const p = typeof k === 'number' ? S[k] : k, c = Math.cos(p.h), s = Math.sin(p.h);
    const V = (al, la, z) => [p.x + c * al - s * la, p.y + s * al + c * la, z];
    const v = [[a0, l0, z0], [a1, l0, z0], [a1, l1, z0], [a0, l1, z0], [a0, l0, z1], [a1, l0, z1], [a1, l1, z1], [a0, l1, z1]].map(q => V(...q));
    const f = fwd(p.h), ln = lnorm(p.h);
    bk.quad(v[0], v[1], v[2], v[3], DOWN); (topB || bk).quad(v[4], v[5], v[6], v[7], UP);
    bk.quad(v[0], v[1], v[5], v[4], neg(ln)); bk.quad(v[3], v[2], v[6], v[7], ln);
    bk.quad(v[0], v[3], v[7], v[4], neg(f)); bk.quad(v[1], v[2], v[6], v[5], f);
  };
  // Surface layers.
  const cc = k => {
    if (state.colour === 0) return null;
    const col = HEAT[state.colour === 1 ? heatCurv(S[k].k, ty) : heatGrade(S[k].g, ty)]; return col;
  };
  for (let k = 0; k + 1 < n; k++) {
    const bk = tunSeg(k) ? B.tunRoad : B.asph, c0 = cc(k), c1 = cc(k + 1);
    bk.quad(P(k, -cw), P(k + 1, -cw), P(k + 1, cw), P(k, cw), UP, c0 ? [c0, c1, c1, c0] : null);
  }
  const surf = k => !tunSeg(k);
  if (!ty.dual) {
    for (const sg of [1, -1]) {
      strip(B.pave, sg * cw, sg * hw, PAVE_H, UP, surf);
      face(B.kerb, sg * cw, 0, PAVE_H, -sg, surf);
      face(B.kerb, sg * hw, -FORM_DROP - 0.05, PAVE_H, sg, surf);
    }
  } else {
    for (const sg of [1, -1]) { strip(B.verge, sg * cw, sg * hw, -0.03, UP, surf); face(B.soil, sg * hw, -FORM_DROP - 0.05, -0.03, sg, surf); face(B.kerb, sg * cw, -0.03, 0, -sg, surf); }
  }
  // Skirts: vertical earth-coloured faces down the outer edge of the ribbon, hiding any residual sub-cell gap (bridge decks have their own sides).
  for (const sg of [1, -1]) face(B.soil, sg * hw, -2.0, -FORM_DROP - 0.04, sg, k => surf(k) && !S[k].br && !S[k + 1].br);
  // Markings (thin white ribbons just above the surface; polygonOffset in the material).
  const MZ = 0.03, MW = 0.08;
  const solid = lat => strip(B.mark, lat - MW, lat + MW, MZ, UP, null);
  const dashed = lat => {
    const L = r.chain.L;
    const mph = ty.speedKph / 1.609, mk = mph <= 40 ? 2 : 3, gp = mk * 2;
    for (let s0 = 1; s0 + mk < L; s0 += mk + gp) {
      const nq = Math.max(2, Math.ceil(mk)), pts = []; for (let q = 0; q <= nq; q++) pts.push(poseAt(r, s0 + mk * q / nq));
      for (let q = 0; q < nq; q++) B.mark.quad(latPt(pts[q], lat - MW, pts[q].z + zo + MZ), latPt(pts[q + 1], lat - MW, pts[q + 1].z + zo + MZ), latPt(pts[q + 1], lat + MW, pts[q + 1].z + zo + MZ), latPt(pts[q], lat + MW, pts[q].z + zo + MZ), UP);
    }
  };
  if (!r.oneWay && !ty.dual) dashed(0);
  else if (ty.lanes > 1) for (let l = 1; l < ty.lanes; l++) dashed(-cw + l * 3.65 + (ty.id === 'motorway' ? 0.7 : 0.5));
  solid(cw - 0.3); solid(-cw + 0.3);

  // Runs of flags.
  const runs = f => { const out = []; let i = 0; while (i < n) { if (!f(S[i])) { i++; continue; } let j = i; while (j + 1 < n && f(S[j + 1])) j++; out.push([i, j]); i = j + 1; } return out; };
  // Bridges: deck slab (1 sample past each end), parapets, piers, abutments.
  const groundMin = (pose, a0, a1, l0, l1) => { // lowest ground (graded or base) under an oriented footprint
    let m = 1e9;
    for (let i = 0; i <= 2; i++) for (let j = 0; j <= 4; j++) {
      const al = a0 + (a1 - a0) * i / 2, la = l0 + (l1 - l0) * j / 4, c = Math.cos(pose.h), sn = Math.sin(pose.h);
      const x = pose.x + c * al - sn * la, y = pose.y + sn * al + c * la; m = Math.min(m, hAt(x, y), hBase(x, y));
    }
    return m;
  };
  for (const [a, b] of runs(s => s.br)) {
    if (b <= a) continue;
    const a2 = Math.max(0, a - 1), b2 = Math.min(n - 1, b + 1);
    extrude(a2, b2, -hw, hw, k => S[k].z + zo - DECK_T, k => S[k].z + zo - 0.03, { noTop: true, bottom: true });
    for (const sg of [1, -1]) extrude(a, b, sg > 0 ? hw - 0.4 : -hw + 0.1, sg > 0 ? hw - 0.1 : -hw + 0.4, k => S[k].z + zo - 0.03, k => S[k].z + zo + PARAPET_H, { top: B.conc });
    const len = S[b].s - S[a].s, usable = len - 16, pw = ty.total * 0.7 / 2;
    if (usable >= 0) {
      const cnt = Math.floor(usable / 35) + 1;
      for (let q = 0; q < cnt; q++) {
        const pp = poseAt(r, S[a].s + 8 + (cnt === 1 ? usable / 2 : usable * q / (cnt - 1)));
        const gnd = groundMin(pp, -0.75, 0.75, -pw, pw), zt = pp.z + zo - DECK_T + 0.05;
        obox(pp, -0.75, 0.75, -pw, pw, Math.min(gnd, zt - 0.5) - 1.0, zt, B.conc);
      }
    }
    for (const [k, a0, a1] of [[a, -1.6, -0.4], [b, 0.4, 1.6]]) {
      const gnd = groundMin(S[k], a0, a1, -hw, hw);
      obox(k, a0, a1, -hw, hw, Math.min(gnd, S[k].z - DECK_T - 0.5) - 1.0, S[k].z + zo - 0.03, B.conc);
    }
  }
  // Retaining walls: 0.4 m thick, face at the road's outer edge; fill sides form a parapet 0.6 m above the road. extrude() closes each run with end faces.
  for (const sg of [1, -1]) {
    const key = sg > 0 ? 'wL' : 'wR', edge = wallLineOf(ty);
    for (const [a, b] of runs(s => s[key] > 0)) {
      if (b <= a) continue;
      const bots = new Float32Array(n), tops = new Float32Array(n);
      for (let k = a; k <= b; k++) {
        const c = latPt(S[k], sg * edge, 0), nat = hBase(c[0], c[1]), form = S[k].z + zo - FORM_DROP;
        bots[k] = Math.min(form, nat) - 1.0; tops[k] = Math.max(S[k].z + zo + 0.6, nat + 0.3);
      }
      const L = sg > 0 ? [edge, edge + 0.4] : [-edge - 0.4, -edge];
      extrude(a, b, L[0], L[1], k => bots[k], k => tops[k], { noTop: true });
    }
  }
  // Tunnels: portal headwalls with a recessed opening, plus a short dark lining so the portal is not see-through.
  for (const [a, b] of runs(s => s.tu)) {
    const pa0 = Math.min(n - 1, a + 1), pb0 = Math.max(0, b - 1), ow = hw + 0.5, W = ty.total / 2 + 3, DEPTH = 1.2;
    const lining = (k0, dir, lim) => { // from the portal sample inwards (dir +1 / -1) for ~10 m, closed by a back wall
      let k1 = k0; while (k1 + dir >= pa0 && k1 + dir <= pb0 && Math.abs(S[k1].s - S[k0].s) < 10) k1 += dir;
      if (k1 === k0) return;
      const lo = Math.min(k0, k1), hi = Math.max(k0, k1);
      for (let k = lo; k < hi; k++) {
        B.tube.quad(P(k, ow, 0), P(k + 1, ow, 0), P(k + 1, ow, TUNNEL_H), P(k, ow, TUNNEL_H), neg(lnorm(S[k].h)));
        B.tube.quad(P(k, -ow, 0), P(k + 1, -ow, 0), P(k + 1, -ow, TUNNEL_H), P(k, -ow, TUNNEL_H), lnorm(S[k].h));
        B.tube.quad(P(k, -ow, TUNNEL_H), P(k + 1, -ow, TUNNEL_H), P(k + 1, ow, TUNNEL_H), P(k, ow, TUNNEL_H), DOWN);
      }
      if (k1 !== (dir > 0 ? pb0 : pa0)) B.tube.quad(P(k1, -ow, -0.6), P(k1, ow, -0.6), P(k1, ow, TUNNEL_H), P(k1, -ow, TUNNEL_H), fwd(S[k1].h).map(v => -v * dir));
    };
    // Portal sits on the last graded approach sample (2nd flagged sample from each end); the ground step lies behind the headwall.
    const pa = pa0, pb = pb0;
    const hwH = (k, a0, a1) => { let m = 0; const p = S[k]; for (const al of [a0, a1]) for (const la of [-W, -ow, 0, ow, W]) { const x = p.x + Math.cos(p.h) * al - Math.sin(p.h) * la, y = p.y + Math.sin(p.h) * al + Math.cos(p.h) * la; m = Math.max(m, hBase(x, y)); } return Math.max(7.0, m - p.z + 1.0); };
    lining(pa, 1, a); lining(pb, -1, b);
    // Earth fill over the graded portal zone (between the cutting walls, above the lining roof) so the graded trench is not open to the sky.
    const wlE = wallLineOf(ty);
    for (const [k0, dir] of [[pa, 1], [pb, -1]]) {
      let k1 = k0; while (k1 + dir >= 0 && k1 + dir < n && S[k1 + dir].tp && S[k1 + dir].tu) k1 += dir;
      if (k1 === k0) continue;
      const lo = Math.min(k0, k1), hi = Math.max(k0, k1), tops = {};
      for (let k = lo; k <= hi; k++) { let m = 1e9; for (const la of [-wlE, 0, wlE]) { const c = latPt(S[k], la, 0); m = Math.min(m, hBase(c[0], c[1])); } tops[k] = Math.max(S[k].z + zo + TUNNEL_H + 0.3, m - 0.1); }
      extrude(lo, hi, -wlE, wlE, k => S[k].z + zo + TUNNEL_H, k => tops[k], { top: B.soil, side: B.soil, noL0: true, noL1: true });
    }
    for (const [k, a0, a1] of [[pa, -DEPTH, 0], [pb, 0, DEPTH]]) {
      const z0 = S[k].z + zo - 0.6, zt = S[k].z + zo + hwH(k, a0, a1);
      obox(k, a0, a1, ow, W, z0, zt, B.conc);
      obox(k, a0, a1, -W, -ow, z0, zt, B.conc);
      obox(k, a0, a1, -ow, ow, S[k].z + zo + TUNNEL_H, zt, B.conc);
    }
  }
  const add = (bk, mat, shadow, pick) => { const m = bk.mesh(mat, shadow); if (m) { m.userData.road = r; m.userData.pick = pick; g.add(m); } };
  add(B.asph, RM.asph, false, true); add(B.tunRoad, RM.asph, false, false);
  add(B.pave, RM.pave, false, true); add(B.verge, RM.verge, false, true); add(B.soil, MAT.soil, false, false); add(B.kerb, RM.kerb, false, false);
  add(B.mark, RM.mark, false, false); add(B.conc, MAT.conc, true, false); add(B.cap, MAT.cap, false, false); add(B.tube, MAT.tube, false, false);
  return g;
}

// Road mesh sync.
const roadGroup = new THREE.Group(); scene.add(roadGroup);
const roadObjs = new Map(); let pickMeshes = [];
function disposeGroup(g) { g.traverse(o => { if (o.geometry) o.geometry.dispose(); }); }
function syncRoads() {
  let changed = false;
  for (const [r, o] of roadObjs) if (!roads.includes(r)) { roadGroup.remove(o.g); disposeGroup(o.g); roadObjs.delete(r); changed = true; }
  for (const r of roads) {
    const o = roadObjs.get(r);
    if (!o || o.ver !== r.ver || o.colour !== state.colour) {
      if (o) { roadGroup.remove(o.g); disposeGroup(o.g); }
      const g = buildRoadGroup(r); roadGroup.add(g); roadObjs.set(r, { g, ver: r.ver, colour: state.colour }); changed = true;
    }
  }
  if (changed) { pickMeshes = []; roadGroup.traverse(o => { if (o.userData.pick) pickMeshes.push(o); }); }
}
