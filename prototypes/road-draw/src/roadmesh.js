// roadmesh.js: materials, mesh helpers and buildRoadGroup (surface, markings, edge lip, retaining walls, bridges, tunnel portals).
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
const COL = { asph: lin('#3c3f44'), pave: lin('#bdbcb7'), kerb: lin('#9d9c96'), hard: lin('#4b4e53'), joint: lin('#7a7872'), mark: lin('#f2f2f2'), conc: lin('#d2d0c9'), concDk: lin('#aeaca5'), tube: lin('#060607'), parapet: lin('#c9c7c0'), wallC: lin('#8a8882'), wallJ: lin('#55534f'), steel: lin('#9aa0a6') };
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
  asph: stdMat({ roughness: 0.9, map: asphTex }), pave: stdMat(), kerb: stdMat(),
  mark: stdMat({ roughness: 0.7, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -4 }),
  conc: stdMat({ roughness: 0.85 }), joint: stdMat({ roughness: 0.9, polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -2 }), tube: stdMat({ roughness: 1 }),
};
// Per-road material variants: higher road id => pulled slightly closer in depth, so overlaps never z-fight; markings always above every surface.
const roadMatCache = new Map();
function roadMats(id) {
  const q = id % 64; let m = roadMatCache.get(q);
  if (!m) {
    const po = (mat, f, u) => { const c = mat.clone(); c.polygonOffset = true; c.polygonOffsetFactor = f; c.polygonOffsetUnits = u; return c; };
    m = { asph: po(MAT.asph, -q * 0.1, -q), pave: po(MAT.pave, -q * 0.1 - 0.05, -q - 0.5), hard: po(MAT.asph, -q * 0.1 - 0.02, -q - 0.2), kerb: po(MAT.kerb, -q * 0.1 - 0.05, -q - 0.5), mark: po(MAT.mark, -10 - q * 0.1, -100 - q) };
    roadMatCache.set(q, m);
  }
  return m;
}
const HEAT = { '#e5484d': lin('#e5484d'), '#f5a623': lin('#f5a623'), '#e8d44d': lin('#e8d44d'), '#3fb950': lin('#3fb950') };
function heatCurv(k, ty) { const R = Math.abs(k) > 1e-9 ? 1 / Math.abs(k) : 1e9; return R < ty.minR * 0.5 ? '#e5484d' : R < ty.minR ? '#f5a623' : R < ty.minR * 2 ? '#e8d44d' : '#3fb950'; }
function heatGrade(g, ty) { const p = Math.abs(g) * 100; return p > ty.maxGrade * 1.5 ? '#e5484d' : p > ty.maxGrade ? '#f5a623' : p > ty.maxGrade * 0.5 ? '#e8d44d' : '#3fb950'; }
const UP = [0, 0, 1], DOWN = [0, 0, -1];
const gAt = (x, y) => typeof groundAt === 'function' ? groundAt(x, y) : hBase(x, y);   // graded ground height (contract from earthworks.js)
const hB = (x, y) => hBase(x, y);

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
  const CB = {}; for (const k of ['wall', 'pier', 'deck', 'abutment', 'parapet', 'portal']) CB[k] = k === 'wall' ? { c: new MB(COL.wallC), d: new MB(COL.wallC), j: new MB(COL.wallJ) } : { c: new MB(COL.conc), d: new MB(COL.concDk), j: new MB(COL.joint) };   // concrete buckets per structure kind (meshes tagged userData.kind)
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
  const surf = k => !tunSeg(k), pav = k => surf(k), onGr = k => pav(k) && !S[k].br && !S[k + 1].br;
  const resid = ty.id === 'residential';
  for (const sg of [1, -1]) {
    if (resid) {   // footways + kerbs: residential only
      strip(B.pave, sg * cw, sg * hw, PAVE_H, UP, pav);
      face(B.kerb, sg * cw, 0, PAVE_H, -sg, pav);
      face(B.kerb, sg * hw, -0.43, PAVE_H, sg, pav);
    } else {       // rural: no kerbs or footways (the verge is graded ground, not a mesh); dual/motorway get a hard shoulder strip
      const hs = ty.dual ? 1.0 : 0, v0 = cw + hs;
      if (hs) strip(B.hard, sg * cw, sg * v0, -0.02, UP, surf);
      strip(B.hard, sg * v0, sg * hw, -0.03, UP, k => surf(k) && (S[k].br || S[k + 1].br));   // deck: asphalt right to the parapet
      if (ty.dual) {   // steel barrier on embankments over 2 m
        const emb = k => onGr(k) && S[k].z - Math.min(hB(...latPt(S[k], sg * hw, 0)), hB(...latPt(S[k + 1], sg * hw, 0))) > 2;
        let k0 = 0;
        while (k0 + 1 < n) { if (!emb(k0)) { k0++; continue; } let k1 = k0; while (k1 + 1 < n - 1 && emb(k1 + 1)) k1++;
          extrude(k0, k1 + 1, sg * (v0 + 0.5), sg * (v0 + 0.65), kk => S[kk].z + zo + 0.45, kk => S[kk].z + zo + 0.85, { top: B.steel, side: B.steel }); k0 = k1 + 1; }
      }
    }
  }
  // Edge lip: 0.4 m downturned asphalt-coloured face at the paved edge so the ground (0.15 m below the footprint) never shows a gap.
  if (!resid) for (const sg of [1, -1]) face(B.hard, sg * (cw + (ty.dual ? 1.0 : 0)), -0.43, -0.02, sg, onGr);
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
  solid(cw - 0.25); solid(-cw + 0.25);   // solid white edge lines (all types)

  // Runs of flags.
  const runs = f => { const out = []; let i = 0; while (i < n) { if (!f(S[i])) { i++; continue; } let j = i; while (j + 1 < n && f(S[j + 1])) j++; out.push([i, j]); i = j + 1; } return out; };
  // Bridges: deck slab (1 sample past each end), parapets, piers, abutments.
  const groundMin = (pose, a0, a1, l0, l1, f = hB) => { // lowest ground (graded or base) under an oriented footprint
    let m = 1e9;
    for (let i = 0; i <= 2; i++) for (let j = 0; j <= 4; j++) {
      const al = a0 + (a1 - a0) * i / 2, la = l0 + (l1 - l0) * j / 4, c = Math.cos(pose.h), sn = Math.sin(pose.h);
      const x = pose.x + c * al - sn * la, y = pose.y + sn * al + c * la; m = Math.min(m, f(x, y));
    }
    return m;
  };
  const gMin2 = (pose, a0, a1, l0, l1) => Math.min(groundMin(pose, a0, a1, l0, l1, gAt), groundMin(pose, a0, a1, l0, l1, hB));   // lowest of graded + base ground under a footprint
  // Oriented prism: along range A0..A1 (signed, from pose p), top height sloping zt0 (at A0) -> zt1 (at A1).
  const sbox = (p, A0, A1, l0, l1, zb, zt0, zt1, bk, topB) => {
    const c = Math.cos(p.h), sn = Math.sin(p.h), V = (al, la, z) => [p.x + c * al - sn * la, p.y + sn * al + c * la, z];
    const v = [[A0, l0, zb], [A1, l0, zb], [A1, l1, zb], [A0, l1, zb], [A0, l0, zt0], [A1, l0, zt1], [A1, l1, zt1], [A0, l1, zt0]].map(q => V(...q));
    const f = fwd(p.h), ln = lnorm(p.h);
    bk.quad(v[0], v[1], v[2], v[3], DOWN); (topB || bk).quad(v[4], v[5], v[6], v[7], UP);
    bk.quad(v[0], v[1], v[5], v[4], neg(ln)); bk.quad(v[3], v[2], v[6], v[7], ln);
    bk.quad(v[0], v[3], v[7], v[4], neg(f)); bk.quad(v[1], v[2], v[6], v[5], f);
  };
  // A span over another road's corridor (it passes >2 m above that road's centreline within both half-widths + 1 m, in plan) or over water (base terrain under the span below T.water) is always a real bridge, however short.
  const spanCrosses = (a, b) => {
    for (let k = a; k <= b; k++) { const s = S[k]; if (hB(s.x, s.y) < T.water + 0.2) return true; }
    for (const r2 of (typeof roads !== 'undefined' ? roads : [])) { if (r2 === r || !r2.samples || r2.samples.length < 2) continue; const lim = hw + ROAD_TYPES[r2.type].total / 2 + 1;
      for (let k = a; k <= b; k++) { const s = S[k]; for (const q of r2.samples) { if (Math.abs(q.x - s.x) > lim || Math.abs(q.y - s.y) > lim) continue; if (Math.hypot(q.x - s.x, q.y - s.y) < lim && s.z - q.z > 2) return true; } } }
    return false;
  };
  const MIN_BRIDGE = 20;   // m of flagged span: shorter runs render as a solid-fill block, not a bridge
  for (const [a, b] of runs(s => s.br)) {
    if (b <= a) continue;
    if (S[b].s - S[a].s < MIN_BRIDGE && !spanCrosses(Math.max(0, a - 1), Math.min(n - 1, b + 1))) {   // too short to be a bridge (fill only briefly exceeds the cap): a solid-fill abutment block with parapets, no deck slab or piers
      const sa = Math.max(0, a - 1), sb = Math.min(n - 1, b + 1);
      extrude(sa, sb, -hw, hw, k => Math.min(gMin2(S[k], -1.0, 1.0, -hw, hw), S[k].z + zo - DECK_T) - 1.0, k => S[k].z + zo - 0.03, { side: CB.abutment.c, noTop: true });
      for (const sg of [1, -1]) extrude(sa, sb, sg > 0 ? hw - 0.4 : -hw + 0.1, sg > 0 ? hw - 0.1 : -hw + 0.4, k => S[k].z + zo - 0.03, k => S[k].z + zo + PARAPET_H, { top: CB.parapet.d, side: CB.parapet.c });
      continue;
    }
    let a2 = Math.max(0, a - 1), b2 = Math.min(n - 1, b + 1); while (S[b2].s - S[a2].s < 24 && (a2 > 0 || b2 < n - 1)) { if (a2 > 0) a2--; if (S[b2].s - S[a2].s >= 24) break; if (b2 < n - 1) b2++; }   // short flagged runs get a sensible >=24 m span, deck+abutments at both ends
    const zD = k => S[k].z + zo;
    // Deck: 0.5 m fascia over the full width, 1.0 m slab body inset 0.7 m each side (reads as a slab, not a wall).
    extrude(a2, b2, -hw, hw, k => zD(k) - 0.5, k => zD(k) - 0.03, { side: CB.deck.c, noTop: true, bottom: true });
    extrude(a2, b2, -hw + 0.7, hw - 0.7, k => zD(k) - DECK_T, k => zD(k) - 0.5, { side: CB.deck.c, noTop: true, bottom: true, noL0: false });
    for (const sg of [1, -1]) extrude(a2, b2, sg > 0 ? hw - 0.4 : -hw + 0.1, sg > 0 ? hw - 0.1 : -hw + 0.4, k => zD(k) - 0.03, k => zD(k) + PARAPET_H, { top: CB.parapet.d, side: CB.parapet.c });
    // Piers every ~32 m: crosshead under the deck, rectangular shaft down to 1 m below base terrain.
    const len = S[b2].s - S[a2].s, spans = Math.max(1, Math.round(len / 32)), pw = hw * 0.55;
    for (let q = 1; q < spans; q++) {
      const pp = poseAt(r, S[a2].s + len * q / spans), zdk = pp.z + zo - DECK_T;
      const gnd = gMin2(pp, -1.2, 1.2, -(hw - 0.9), hw - 0.9);
      obox(pp, -0.7, 0.7, -pw, pw, Math.min(gnd, zdk - 1.0) - 1.0, zdk - 1.0, CB.pier.c);
      obox(pp, -1.2, 1.2, -(hw - 0.9), hw - 0.9, zdk - 1.0, zdk, CB.pier.d);   // crosshead / pier cap
    }
    // Closed abutments at both ends: bearing wall + ballast wall + short wing walls following the embankment slope.
    for (const [k, dr] of [[a2, -1], [b2, 1]]) {
      const p = S[k], zr = p.z + zo, A = (x, y) => dr > 0 ? [x, y] : [-y, -x];
      const gnd = Math.min(gMin2(p, ...A(-1.4, 0.6), -hw - 0.5, hw + 0.5), gMin2(p, ...A(0.6, 6.5), -hw - 0.5, hw + 0.5)), zbA = Math.min(gnd, zr - DECK_T - 0.5) - 1.0;
      obox(p, ...A(-1.4, 0.0), -hw, hw, zbA, zr - DECK_T, CB.abutment.c);                  // front (bearing) wall under the deck end
      obox(p, ...A(0.0, 0.5), -hw, hw, zbA, zr - 0.03, CB.abutment.c, CB.abutment.d);           // ballast wall up to road level
      for (const sg of [1, -1]) {                                                  // wing walls, top sloping 1:2 down into the fill
        const l0 = sg > 0 ? hw - 0.1 : -hw - 0.4, l1 = sg > 0 ? hw + 0.4 : -hw + 0.1;
        const fe = fwd(p.h), gEs = [l0, l1].map(l => { const le = latPt(p, l, 0); return gAt(le[0] + fe[0] * dr * 6.5, le[1] + fe[1] * dr * 6.5); }), gE = Math.min(...gEs), gS = Math.min(...[-1.4, 0.5].flatMap(al => [l0, l1].map(l => { const q = latPt(p, l, 0); return gAt(q[0] + fe[0] * dr * al, q[1] + fe[1] * dr * al); }))), zw = Math.max(zbA + 0.5, Math.min(zr - 0.03, gS + 0.25));   // tie the wing top into the graded embankment
        sbox(p, dr * 0.5, dr * 6.5, l0, l1, zbA, zw, clamp(gE + 0.1, zbA + 0.5, zw), CB.abutment.c, CB.abutment.d);
        sbox(p, dr * -1.4, dr * 0.5, l0, l1, zbA, zw, zw, CB.abutment.c, CB.abutment.d);
      }
    }
  }
  // ===== Retaining walls (from wallRuns) and tunnel portals (headwall + wing walls + dark lining) =====
  const WALL_T = 1.5, CAP_H = 0.5, CAP_O = 0.1, TAPER_L = 4.0;
  const hdir = (a, b) => { const dx = b[0] - a[0], dy = b[1] - a[1], l = Math.hypot(dx, dy) || 1; return [dx / l, dy / l, 0]; };
  // Wall + coping along a station list: {a:[x,y] inner face point, u:[x,y] outward unit, zb, zt (top of coping)}.
  const wallStations = (st0, WB = CB.wall) => {
    // drop stations where the offset (outer) face folds back relative to the inner line (inside of tight curves), or the line reverses
    const st = []; for (const q of st0) {
      const pr = st[st.length - 1]; if (!pr) { st.push(q); continue; }
      if (q.dup) { if (pr.a[0] === q.a[0] && pr.a[1] === q.a[1]) st.push(q); continue; }   // panel step: second station at the same place, new top level
      const dA = [q.a[0] - pr.a[0], q.a[1] - pr.a[1]], dB = [q.a[0] + q.u[0] * WALL_T - pr.a[0] - pr.u[0] * WALL_T, q.a[1] + q.u[1] * WALL_T - pr.a[1] - pr.u[1] * WALL_T];
      const la = Math.hypot(...dA), lb = Math.hypot(...dB);
      if (la < 1e-6 || lb < 0.25 * la || dA[0] * dB[0] + dA[1] * dB[1] < 0.3 * la * lb) continue;
      if (st.length > 1) { const pp = st[st.length - 2], dP = [pr.a[0] - pp.a[0], pr.a[1] - pp.a[1]]; if (dP[0] * dA[0] + dP[1] * dA[1] < 0) continue; }
      st.push(q);
    }
    
    if (st.length < 2) return;
    { let L = 0; for (let i = 1; i < st.length; i++) L += Math.hypot(st[i].a[0] - st[i - 1].a[0], st[i].a[1] - st[i - 1].a[1]); if (L < 6 && WB === CB.wall) return; }
    let acc = 0, nextJ = 3;
    const W3 = (q, off, z) => [q.a[0] + q.u[0] * off, q.a[1] + q.u[1] * off, z];
    const cph = q => Math.min(CAP_H, Math.max(0.05, (q.zt - q.zb) * 0.4)), wt = q => q.zt - cph(q);
    for (let i = 0; i + 1 < st.length; i++) {
      const p = st[i], q = st[i + 1], hi = [p.u[0], p.u[1], 0], lo = [-p.u[0], -p.u[1], 0];
      const sl = Math.hypot(q.a[0] - p.a[0], q.a[1] - p.a[1]);
      if (q.dup && sl < 1e-9) {   // closed vertical face at a panel step, facing the lower panel
        const tg = hdir(st[Math.max(0, i - 1)].a, p.a), lz = Math.min(p.zt, q.zt), hz = Math.max(p.zt, q.zt), sgn = q.zt > p.zt ? -1 : 1;
        WB.c.quad(W3(p, 0, lz), W3(p, WALL_T, lz), W3(p, WALL_T, hz), W3(p, 0, hz), tg.map(v => v * sgn)); continue;
      }
      while (nextJ <= acc + sl) {   // vertical construction joint every 8 m on the visible face
        const t = (nextJ - acc) / (sl || 1), d = [(q.a[0] - p.a[0]) / (sl || 1) * 0.04, (q.a[1] - p.a[1]) / (sl || 1) * 0.04];
        const jq = { zb: p.zb + (q.zb - p.zb) * t, zt: p.zt + (q.zt - p.zt) * t }, zt = wt(jq), o = -0.012;
        const V = (sx, z) => [p.a[0] + (q.a[0] - p.a[0]) * t + p.u[0] * o + d[0] * sx, p.a[1] + (q.a[1] - p.a[1]) * t + p.u[1] * o + d[1] * sx, z];
        if (zt > jq.zb + 0.05) WB.j.quad(V(-1, jq.zb), V(1, jq.zb), V(1, zt), V(-1, zt), lo);
        nextJ += 3;
      }
      acc += sl;
      WB.c.quad(W3(p, 0, p.zb), W3(q, 0, q.zb), W3(q, 0, wt(q)), W3(p, 0, wt(p)), lo);
      WB.c.quad(W3(p, WALL_T, p.zb), W3(q, WALL_T, q.zb), W3(q, WALL_T, wt(q)), W3(p, WALL_T, wt(p)), hi);
      const c0 = 0, c1 = WALL_T;   // cap flush with the body (same mesh, so every column reaches the foot)
      WB.c.quad(W3(p, c0, p.zt), W3(q, c0, q.zt), W3(q, c1, q.zt), W3(p, c1, p.zt), UP);
      WB.c.quad(W3(p, c0, wt(p)), W3(q, c0, wt(q)), W3(q, c0, q.zt), W3(p, c0, p.zt), lo);
      WB.c.quad(W3(p, c1, wt(p)), W3(q, c1, wt(q)), W3(q, c1, q.zt), W3(p, c1, p.zt), hi);
    }
    for (const [i, sgn] of [[0, -1], [st.length - 1, 1]]) {   // end faces
      const p = st[i], d = hdir(st[Math.max(0, i - 1)].a, st[Math.min(st.length - 1, i + 1)].a).map(v => v * sgn);
      WB.c.quad(W3(p, 0, p.zb), W3(p, WALL_T, p.zb), W3(p, WALL_T, wt(p)), W3(p, 0, wt(p)), d);
      WB.c.quad(W3(p, 0, wt(p)), W3(p, WALL_T, wt(p)), W3(p, WALL_T, p.zt), W3(p, 0, p.zt), d);
    }
  };
  const resampleRun = (pts, step = 0.5) => {
    const out = [];
    for (let i = 0; i + 1 < pts.length; i++) {
      const a = pts[i], b = pts[i + 1], m = Math.max(1, Math.ceil(Math.hypot(b.x - a.x, b.y - a.y) / step));
      for (let j = 0; j < m; j++) { const t = j / m; out.push({ x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t, top: a.top + (b.top - a.top) * t, zTop: a.zTop + (b.zTop - a.zTop) * t, zBot: a.zBot + (b.zBot - a.zBot) * t, h: a.h }); }
    }
    out.push({ ...pts[pts.length - 1] });
    return out;
  };

  // Portals: flagged stations (fallback: tunnel run ends); dir = direction of increasing index that points into the tunnel.
  const portals = [];
  { const flagged = S.some(s => s.portal);
    if (flagged) {
      for (let k = 0; k < n; k++) if (S[k].portal) {
        const nx = k + 1 < n && S[k + 1].tu, pv = k > 0 && S[k - 1].tu;
        portals.push({ p: k, dir: nx && !pv ? 1 : pv && !nx ? -1 : (k < n / 2 ? 1 : -1) });
      }
    } else for (const [a, b] of runs(s => s.tu)) { portals.push({ p: a, dir: 1 }); portals.push({ p: b, dir: -1 }); }
  }
  const edgeW = hw + 0.2, Wd = hw + 6, Wf = Wd - 0.45, OW = (ty.total + 1) / 2, WING_ANG = Math.PI / 6;
  const wingLen = (Wf - edgeW) / Math.tan(WING_ANG);
  const WR = typeof wallRuns === 'function' ? (wallRuns(r) || []) : [];
  const nearPortal = (x, y) => portals.some(pt => Math.hypot(x - S[pt.p].x, y - S[pt.p].y) < wingLen + 6);
  const brZone = []; for (const [a, b] of runs(s => s.br)) if (b > a) brZone.push({ a: Math.max(0, a - 1), b: Math.min(n - 1, b + 1) });
  const inBridgeZone = (x, y) => brZone.some(z => { for (let k = z.a; k <= z.b; k++) { const d = Math.hypot(x - S[k].x, y - S[k].y); if (d < hw + 3 && (k > z.a && k < z.b)) return true; if ((k === z.a || k === z.b) && d < 8) return true; } return false; });   // over the span, or within 8 m of an abutment
  for (const run of WR) {
    if (!run.pts || run.pts.length < 2) continue;
    const sg = run.side === 'L' ? 1 : -1;
    // drop wall that would lie inside a portal's wing-wall zone (the wing walls continue it flush)
    const keep = q => !portals.some(pt => Math.hypot(q.x - S[pt.p].x, q.y - S[pt.p].y) < wingLen * 0.97) && !inBridgeZone(q.x, q.y);   // portal wing zone / bridge span + abutment zone
    const segs = []; { let cur = []; for (const q of run.pts) { if (keep(q)) cur.push(q); else { if (cur.length) segs.push(cur); cur = []; } } if (cur.length) segs.push(cur); }
    for (const pts of segs) {
    if (pts.length < 2) continue;
    const rs = resampleRun(pts);
    const m = rs.length, cum = [0]; for (let i = 1; i < m; i++) cum.push(cum[i - 1] + Math.hypot(rs[i].x - rs[i - 1].x, rs[i].y - rs[i - 1].y));
    const tot = cum[m - 1], tapA = !nearPortal(rs[0].x, rs[0].y), tapB = !nearPortal(rs[m - 1].x, rs[m - 1].y), st = [];
    const gHi = rs.map((q, i) => { const a = rs[Math.max(0, i - 1)], b = rs[Math.min(m - 1, i + 1)], t = hdir([a.x, a.y], [b.x, b.y]); let u = [-t[1] * sg, t[0] * sg];
      if (q.h !== undefined && u[0] * -Math.sin(q.h) * sg + u[1] * Math.cos(q.h) * sg < 0) u = [-u[0], -u[1]];
      return Math.max(gAt(q.x, q.y), gAt(q.x + u[0] * WALL_T, q.y + u[1] * WALL_T)); });
    const gSm = gHi.map((g, i) => (gHi[Math.max(0, i - 1)] + g + gHi[Math.min(m - 1, i + 1)]) / 3);   // smoothed ground at the wall: ends taper to it
    const topRaw = [], zbA = [], uA = [];
    for (let i = 0; i < m; i++) {
      const q = rs[i], a = rs[Math.max(0, i - 1)], b = rs[Math.min(m - 1, i + 1)], t = hdir([a.x, a.y], [b.x, b.y]);
      let u = [-t[1] * sg, t[0] * sg];
      if (q.h !== undefined && u[0] * -Math.sin(q.h) * sg + u[1] * Math.cos(q.h) * sg < 0) u = [-u[0], -u[1]];
      const gi = gAt(q.x, q.y), go = gAt(q.x + u[0] * WALL_T, q.y + u[1] * WALL_T), zb = Math.min(q.zBot, gi, go, hB(q.x, q.y), hB(q.x + u[0] * WALL_T, q.y + u[1] * WALL_T)) - 1.0, top = Math.min(q.top, q.zTop + 0.7, Math.max(Math.min(gi, go) + 0.55, zb + 0.1));   // fill wall: barrier-height cap (+0.55 above road level)
      const topC = run.cut ? Math.max(...[0, 0.5, 1.0, 1.5].map(e => gAt(q.x + u[0] * e, q.y + u[1] * e))) + 0.3 : top;   // cut wall: just clear the ACTUAL graded ground (lower than natural where the wall tapers out at a run end)
      topRaw.push(topC); zbA.push(zb); uA.push(u);
    }
    // Clean foundation line: moving-min then moving-average of the foot (+-4 m). Never above the per-station foot, so still below the ground everywhere, but no 0.5 m grid sawtooth.
    { const W = 8, mn = zbA.map((_, i) => { let v = 1e9; for (let j = Math.max(0, i - W); j <= Math.min(m - 1, i + W); j++) v = Math.min(v, zbA[j]); return v; });
      for (let i = 0; i < m; i++) { let sum = 0, c = 0; for (let j = Math.max(0, i - W); j <= Math.min(m - 1, i + W); j++) { sum += mn[j]; c++; } zbA[i] = sum / c; } }
    // Stepped coping: fixed-length panels (PANEL_L m), each with a level top quantised UP to PANEL_Q m. A panel's required top is the max of the raw tops over its stations (so retained ground / road edge is always covered).
    // Hysteresis: the top rises as soon as a panel needs it, but only steps down when the lower need persists for two panels (no single-panel dip). A step is two stations at one place with a closed vertical face (see wallStations).
    const PANEL_L = 4, PANEL_Q = 0.25, nP = Math.max(1, Math.round(tot / PANEL_L)), pIdx = i => Math.min(nP - 1, Math.floor(cum[i] / (tot / nP) + 1e-9));
    const need = new Array(nP).fill(-1e9); for (let i = 0; i < m; i++) { const p = pIdx(i); need[p] = Math.max(need[p], topRaw[i], zbA[i] + 0.3); }
    const lvl = need.map(v => Math.ceil((v - 1e-6) / PANEL_Q) * PANEL_Q), pl = [lvl[0]];
    for (let p = 1, cur = lvl[0]; p < nP; p++) { if (lvl[p] > cur) cur = lvl[p]; else if (lvl[p] < cur) { const tg = Math.max(lvl[p], p + 1 < nP ? lvl[p + 1] : lvl[p]); if (tg < cur) cur = tg; } pl.push(cur); }
    for (let i = 0; i < m; i++) {
      const p = pIdx(i), pp = i > 0 ? pIdx(i - 1) : p, a = [rs[i].x, rs[i].y];
      if (p !== pp && pl[p] !== pl[pp]) { st.push({ a, u: uA[i], zb: zbA[i], zt: pl[pp] }); st.push({ a, u: uA[i], zb: zbA[i], zt: pl[p], dup: true }); }
      else st.push({ a, u: uA[i], zb: zbA[i], zt: pl[p] });
    }
    wallStations(st);
    }
  }
  // Tunnel portals.
  const OPEN_H = TUNNEL_H, LIN = 15, REC = 0.5, HEAD_T = 2.0, CAP_P = 0.5;
  for (const pt of portals) {
    const { p, dir } = pt, P0 = S[p], zr = P0.z + zo, c = Math.cos(P0.h), sn = Math.sin(P0.h);
    const alOf = (al, la) => [P0.x + c * al * dir - sn * la, P0.y + sn * al * dir + c * la];
    // headwall height: cover natural ground across the full width (at the face and just behind it) + 1 m
    let gmn = 1e9; for (const al of [0, HEAD_T]) for (let la = -Wd; la <= Wd + 1e-6; la += 1) { const q = alOf(al, la); gmn = Math.min(gmn, hB(q[0], q[1]), gAt(q[0], q[1])); }
    const zb = Math.min(zr - 2.5, gmn - 1.0);
    const ob = (a0, a1, l0, l1, z0, z1, bk) => obox(P0, Math.min(a0 * dir, a1 * dir), Math.max(a0 * dir, a1 * dir), l0, l1, z0, z1, bk);
    // headwall in 0.5 m lateral strips: each strip's top follows the graded ground (+0.15 m), or the lintel height over the opening
    const NSTR = Math.ceil(2 * Wd / 0.5), SW = 2 * Wd / NSTR;
    const sl = (al0, al1, l0, l1, z0, t0, t1, bk, topB) => dir > 0 ? sbox(P0, al0, al1, l0, l1, z0, t0, t1, bk, topB) : sbox(P0, -al1, -al0, l0, l1, z0, t1, t0, bk, topB);   // sloping-top prism, alongs measured into the tunnel
    for (let i = 0; i < NSTR; i++) {
      const l0 = -Wd + i * SW, l1 = l0 + SW, inOpen = l0 >= -OW - 1e-6 && l1 <= OW + 1e-6, lo = inOpen ? zr + OPEN_H + 0.5 : zr + 0.2;
      const tp = al => { let g = -1e9; for (const la of [l0, l1]) { const q = alOf(al, la); g = Math.max(g, gAt(q[0], q[1])); } return inOpen ? g + 0.15 : Math.max(g + 0.15, lo); };
      const tF = tp(-CAP_O), tB = tp(HEAD_T + CAP_O), at = al => tF + (tB - tF) * (al + CAP_O) / (HEAD_T + 2 * CAP_O);
      if (inOpen && Math.min(tF, tB) < lo) continue;   // graded ground is below the lintel here (open cutting): nothing to carry
      const lc0 = i === 0 ? l0 - CAP_O : l0, lc1 = i === NSTR - 1 ? l1 + CAP_O : l1;
      sl(0, HEAD_T, l0, l1, inOpen ? zr + OPEN_H : zb, at(0) - CAP_P, at(HEAD_T) - CAP_P, CB.portal.c);
      sl(-CAP_O, HEAD_T + CAP_O, lc0, lc1, Math.min(tF, tB) - CAP_P - 0.0, tF, tB, CB.portal.d);
    }
    // lining: stations by arc length, near-black, recessed
    const poses = []; for (let d = REC; d <= REC + LIN + 1e-6; d += 2) poses.push(poseAt(r, P0.s + dir * Math.min(d, REC + LIN)));
    const LW = OW - 0.02, ZR = TUNNEL_H - 0.02;
    for (let i = 0; i + 1 < poses.length; i++) {
      const p0 = poses[i], p1 = poses[i + 1], z = q => q.z + zo;
      B.tube.quad(latPt(p0, LW, z(p0)), latPt(p1, LW, z(p1)), latPt(p1, LW, z(p1) + ZR), latPt(p0, LW, z(p0) + ZR), lnorm(p0.h).map(v => -v));
      B.tube.quad(latPt(p0, -LW, z(p0)), latPt(p1, -LW, z(p1)), latPt(p1, -LW, z(p1) + ZR), latPt(p0, -LW, z(p0) + ZR), lnorm(p0.h));
      B.tube.quad(latPt(p0, -LW, z(p0) + ZR), latPt(p1, -LW, z(p1) + ZR), latPt(p1, LW, z(p1) + ZR), latPt(p0, LW, z(p0) + ZR), DOWN);
      for (const sg of [1, -1]) B.tube.quad(latPt(p0, sg * cw, z(p0) - 0.02), latPt(p1, sg * cw, z(p1) - 0.02), latPt(p1, sg * LW, z(p1) - 0.02), latPt(p0, sg * LW, z(p0) - 0.02), UP);
    }
    const pe = poses[poses.length - 1];
    B.tube.quad(latPt(pe, -LW, pe.z + zo - 0.02), latPt(pe, LW, pe.z + zo - 0.02), latPt(pe, LW, pe.z + zo + ZR), latPt(pe, -LW, pe.z + zo + ZR), fwd(pe.h).map(v => -v * dir));
    // mouth cap: the graded ground is a heightfield, so it can still rise across the opening a few metres into the bore (the grading steps up from formation to natural there).
    // An opaque dark panel just in front of that rise (at most HEAD_T deep, so the headwall reveal stays visible) guarantees you only ever see a dark lined tube, never terrain or sky through the opening.
    { let capAl = HEAD_T; for (let al = 0.5; al <= HEAD_T + 6; al += 0.5) { let hit = false; for (const la of [-OW * 0.8, 0, OW * 0.8]) { const q = alOf(al, la); if (gAt(q[0], q[1]) > zr + 0.3) hit = true; } if (hit) { capAl = Math.max(0.3, Math.min(HEAD_T, al - 0.6)); break; } }
      const pc = poseAt(r, P0.s + dir * capAl), zc = pc.z + zo;
      B.tube.quad(latPt(pc, -LW, zc - 0.02), latPt(pc, LW, zc - 0.02), latPt(pc, LW, zc + ZR), latPt(pc, -LW, zc + ZR), fwd(pc.h).map(v => -v * dir)); }
    // wing walls: symmetric 30 deg splay from the approach wall line out to the headwall edges; start flush with the approach wall (wallRuns) if present
    const sApp = clamp(P0.s - dir * wingLen, S[0].s, S[n - 1].s), Lw = Math.abs(P0.s - sApp), NS = Math.max(2, Math.ceil(Lw / 1.5));
    for (const sg of [1, -1]) {
      const pa = poseAt(r, sApp); let topA = pa.z + zo + 1.0, botA = pa.z + zo - 1.5, edge0 = edgeW, best = 3;
      for (const run of WR) if (run.side === (sg > 0 ? 'L' : 'R')) for (const q of run.pts) {
        const d = Math.hypot(q.x - pa.x, q.y - pa.y);
        if (d < best) { best = d; topA = q.zTop; botA = q.zBot; edge0 = Math.abs((q.x - pa.x) * -Math.sin(pa.h) + (q.y - pa.y) * Math.cos(pa.h)); }
      }
      if (best >= 3) { const g = gAt(...latPt(pa, sg * (edgeW + 2), 0).slice(0, 2)); topA = Math.max(topA, Math.min(g, topA + 3)); }
      const tg = Lw > 0.1 ? (Wf - edge0) / Lw : Math.tan(WING_ANG), ca = 1 / Math.hypot(1, tg), sa = tg * ca;
      const st = [];
      for (let i = 0; i <= NS; i++) {
        const t = i / NS, ps = poseAt(r, sApp + (P0.s - sApp) * t), lat = edge0 + (Wf - edge0) * t, ln = lnorm(ps.h), f = fwd(ps.h), cp = latPt(ps, sg * lat, 0);
        const u = [(-dir * sa) * f[0] + ca * sg * ln[0], (-dir * sa) * f[1] + ca * sg * ln[1]], ul = Math.hypot(u[0], u[1]);
        const wzb = Math.min(botA * (1 - t) + zb * t, hB(cp[0], cp[1]) - 1, gAt(cp[0], cp[1]) - 1);
        st.push({ a: [cp[0], cp[1]], u: [u[0] / ul, u[1] / ul], zb: wzb, zt: Math.max(wzb + 0.5, Math.min(...[-0.1, 0, 0.75, 1.5, 1.6].map(o => gAt(cp[0] + u[0] / ul * o, cp[1] + u[1] / ul * o))) + 0.5) });
      }
      wallStations(st, CB.portal);
    }
  }
  const add = (bk, mat, shadow, pick, kind = 'surface') => { const m = bk.mesh(mat, shadow); if (m) { m.userData.road = r; m.userData.pick = pick; m.userData.kind = kind; g.add(m); } };
  add(B.asph, RM.asph, false, true); add(B.tunRoad, RM.asph, false, false);
  add(B.pave, RM.pave, false, true); add(B.kerb, RM.kerb, false, false);
  add(B.mark, RM.mark, false, false); add(B.steel, MAT.conc, true, false, 'barrier'); add(B.hard, RM.hard, false, true); add(B.tube, MAT.tube, false, false, 'lining');
  for (const k of Object.keys(CB)) { add(CB[k].j, MAT.joint, false, false, k); add(CB[k].c, MAT.conc, true, false, k); add(CB[k].d, MAT.conc, true, false, k); }
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
