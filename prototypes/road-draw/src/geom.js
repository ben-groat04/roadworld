// ===== Geometry (pure, no DOM). Mirrors sim_core clothoid / clothoid_chain / vertical_profile. =====
const G = (() => {
  const PI = Math.PI;
  const wrap = a => { a = (a + PI) % (2 * PI); if (a <= 0) a += 2 * PI; return a - PI; };
  const GX = [0.1488743389816312, 0.4333953941292472, 0.6794095682990244, 0.8650633666889845, 0.9739065285171717];
  const GW = [0.2955242247147529, 0.2692667193099963, 0.2190863625159820, 0.1494513491505806, 0.0666713443086881];

  // int_0^1 t^m (cos, sin)(a/2 t^2 + b t + c) dt, for m = 0, 1, 2 at once.
  function fres(a, b, c) {
    const n = Math.min(4096, Math.max(1, Math.ceil((Math.abs(a) + Math.abs(b)) / 0.5)));
    const h = 1 / n, half = h / 2;
    let X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, X2 = 0, Y2 = 0;
    for (let i = 0; i < n; i++) {
      const mid = (i + 0.5) * h;
      for (let j = 0; j < 5; j++) for (let sg = -1; sg <= 1; sg += 2) {
        const t = mid + sg * half * GX[j], ph = (0.5 * a * t + b) * t + c, w = GW[j] * half;
        const co = Math.cos(ph) * w, si = Math.sin(ph) * w;
        X0 += co; Y0 += si; X1 += co * t; Y1 += si * t; X2 += co * t * t; Y2 += si * t * t;
      }
    }
    return [X0, Y0, X1, Y1, X2, Y2];
  }

  const curv = (c, s) => c.k0 + c.dk * s;
  const head = (c, s) => c.h0 + c.k0 * s + 0.5 * c.dk * s * s;
  function point(c, s) {
    if (s === 0) return [c.x0, c.y0];
    const f = fres(c.dk * s * s, c.k0 * s, c.h0);
    return [c.x0 + s * f[0], c.y0 + s * f[1]];
  }

  // G1 Hermite clothoid (Bertolazzi & Frego 2015).
  function fitG1(x0, y0, t0, x1, y1, t1) {
    const dx = x1 - x0, dy = y1 - y0, r = Math.hypot(dx, dy);
    if (!(r > 1e-9)) return null;
    const phi = Math.atan2(dy, dx), p0 = wrap(t0 - phi), p1 = wrap(t1 - phi), d = p1 - p0;
    let A = 3 * (p0 + p1);
    for (let it = 0; it < 50; it++) {
      const f = fres(2 * A, d - A, p0), g = f[1], dg = f[4] - f[2];
      if (Math.abs(g) < 1e-14 || dg === 0) break;
      const st = Math.max(-1, Math.min(1, g / dg));
      A -= st;
      if (Math.abs(st) < 1e-15 * Math.max(1, Math.abs(A))) break;
    }
    const X = fres(2 * A, d - A, p0)[0];
    if (!(X > 0)) return null;
    const L = r / X;
    return { x0, y0, h0: t0, k0: (d - A) / L, dk: 2 * A / (L * L), L };
  }

  function solve(A, b, n) {
    for (let c = 0; c < n; c++) {
      let p = c;
      for (let r = c + 1; r < n; r++) if (Math.abs(A[r * n + c]) > Math.abs(A[p * n + c])) p = r;
      if (Math.abs(A[p * n + c]) < 1e-300) return false;
      if (p !== c) { for (let k = 0; k < n; k++) { const t = A[c * n + k]; A[c * n + k] = A[p * n + k]; A[p * n + k] = t; } const t = b[c]; b[c] = b[p]; b[p] = t; }
      for (let r = c + 1; r < n; r++) { const f = A[r * n + c] / A[c * n + c]; for (let k = c; k < n; k++) A[r * n + k] -= f * A[c * n + k]; b[r] -= f * b[c]; }
    }
    for (let r = n - 1; r >= 0; r--) { let s = b[r]; for (let k = r + 1; k < n; k++) s -= A[r * n + k] * b[k]; b[r] = s / A[r * n + r]; }
    return true;
  }

  // G2 chain through points [[x,y],...]. hs/he: pinned headings or null (natural end, k = 0).
  function fitChain(P, hs = null, he = null) {
    const n = P.length;
    if (n < 2) return null;
    for (let i = 0; i + 1 < n; i++) if (Math.hypot(P[i + 1][0] - P[i][0], P[i + 1][1] - P[i][1]) < 1e-6) return null;
    const chord = (a, b) => Math.atan2(P[b][1] - P[a][1], P[b][0] - P[a][0]);
    const h = new Array(n).fill(0);
    for (let i = 1; i + 1 < n; i++) {
      const lu = Math.hypot(P[i][0] - P[i - 1][0], P[i][1] - P[i - 1][1]), lv = Math.hypot(P[i + 1][0] - P[i][0], P[i + 1][1] - P[i][1]);
      const hu = chord(i - 1, i), hv = chord(i, i + 1);
      h[i] = hu + wrap(hv - hu) * lu / (lu + lv);
    }
    h[0] = n > 2 ? chord(0, 1) - wrap(h[1] - chord(0, 1)) : chord(0, 1);
    h[n - 1] = n > 2 ? chord(n - 2, n - 1) + wrap(chord(n - 2, n - 1) - h[n - 2]) : chord(0, 1);
    if (hs != null) h[0] = hs;
    if (he != null) h[n - 1] = he;
    for (let i = 1; i < n; i++) { const c = chord(i - 1, i); h[i] = c + wrap(h[i] - c); }
    h[0] = chord(0, 1) + wrap(h[0] - chord(0, 1));

    const free = [];
    for (let i = 0; i < n; i++) if (!(i === 0 && hs != null) && !(i === n - 1 && he != null)) free.push(i);
    const build = hh => { const sp = []; for (let i = 0; i + 1 < n; i++) { const c = fitG1(P[i][0], P[i][1], hh[i], P[i + 1][0], P[i + 1][1], hh[i + 1]); if (!c) return null; sp.push(c); } return sp; };
    const resid = hh => {
      const sp = build(hh); if (!sp) return null;
      return free.map(i => i === 0 ? sp[0].k0 : i === n - 1 ? curv(sp[n - 2], sp[n - 2].L) : curv(sp[i - 1], sp[i - 1].L) - sp[i].k0);
    };
    const n2 = v => v.reduce((a, x) => a + x * x, 0);
    const h0 = h.slice(), m = free.length;
    let r = resid(h), ok = !!r, conv = ok && (m === 0 || n2(r) < 1e-20);
    for (let it = 0; ok && !conv && it < 40; it++) {
      const J = new Float64Array(m * m), eps = 1e-7;
      for (let j = 0; j < m; j++) {
        const hp = h.slice(); hp[free[j]] += eps;
        const rp = resid(hp); if (!rp) { ok = false; break; }
        for (let i = 0; i < m; i++) J[i * m + j] = (rp[i] - r[i]) / eps;
      }
      if (!ok) break;
      const step = r.slice();
      if (!solve(J, step, m)) { ok = false; break; }
      let t = 1, improved = false; const r0 = n2(r);
      for (let ls = 0; ls < 20; ls++, t *= 0.5) {
        const hn = h.slice();
        for (let j = 0; j < m; j++) hn[free[j]] -= Math.max(-0.5, Math.min(0.5, t * step[j]));
        const rn = resid(hn);
        if (rn && n2(rn) < r0) { for (let k = 0; k < n; k++) h[k] = hn[k]; r = rn; improved = true; break; }
      }
      if (!improved) break;
      conv = n2(r) < 1e-20;
    }
    let spans = ok ? build(h) : null;
    if (!spans) spans = build(h0);
    if (!spans) return null;
    let kink = 0;
    for (let i = 1; i < spans.length; i++) kink = Math.max(kink, Math.abs(curv(spans[i - 1], spans[i - 1].L) - spans[i].k0));
    const L = spans.reduce((a, c) => a + c.L, 0);
    return { spans, g2: (ok && conv) || kink < 1e-9, kink, L };
  }

  // Locate span for chain arc length s.
  function at(ch, s) {
    let acc = 0;
    for (let i = 0; i < ch.spans.length; i++) {
      const c = ch.spans[i];
      if (s <= acc + c.L || i === ch.spans.length - 1) return [c, Math.max(0, Math.min(c.L, s - acc))];
      acc += c.L;
    }
  }
  function chainPose(ch, s) { const [c, u] = at(ch, s); const p = point(c, u); return { x: p[0], y: p[1], h: head(c, u), k: curv(c, u) }; }

  // Sample a chain incrementally (cheap: integrates heading with midpoint steps per span).
  function sample(ch, maxStep = 2) {
    const out = []; let acc = 0;
    for (let i = 0; i < ch.spans.length; i++) {
      const c = ch.spans[i];
      const n = Math.max(2, Math.ceil(c.L / maxStep), Math.ceil(Math.abs(c.k0 * c.L + 0.5 * c.dk * c.L * c.L) / 0.05));
      const startJ = i === 0 ? 0 : 1;
      for (let j = startJ; j <= n; j++) {
        const u = c.L * j / n, p = point(c, u);
        out.push({ s: acc + u, x: p[0], y: p[1], h: head(c, u), k: curv(c, u), span: i });
      }
      acc += c.L;
    }
    return out;
  }

  // Vertical profile: constant grades between control points, parabolic curves L = K*|dg%| at breaks.
  function profile(S, Z, kCrest, kSag) {
    const b = S.map((s, i) => ({ s, z: Z[i], L: 0 }));
    const gr = (i, j) => (b[j].z - b[i].z) / (b[j].s - b[i].s);
    for (let i = 1; i + 1 < b.length; i++) {
      const dg = (gr(i, i + 1) - gr(i - 1, i)) * 100;
      if (Math.abs(dg) < 1e-9) continue;
      const K = dg < 0 ? kCrest : kSag;
      b[i].L = Math.min(K * Math.abs(dg), b[i].s - b[i - 1].s, b[i + 1].s - b[i].s);
    }
    return b;
  }
  function profZ(b, s) {
    if (b.length === 1) return b[0].z;
    const gr = (i, j) => (b[j].z - b[i].z) / (b[j].s - b[i].s);
    for (let j = 1; j + 1 < b.length; j++) {
      const v = b[j], hh = v.L / 2;
      if (hh <= 0 || s < v.s - hh || s > v.s + hh) continue;
      const g0 = gr(j - 1, j), g1 = gr(j, j + 1), x = s - (v.s - hh);
      return v.z - g0 * hh + g0 * x + (g1 - g0) * x * x / (2 * v.L);
    }
    let i = 0; while (i + 2 < b.length && s > b[i + 1].s) i++;
    return b[i].z + gr(i, i + 1) * (s - b[i].s);
  }
  function profGrade(b, s) {
    if (b.length < 2) return 0;
    const gr = (i, j) => (b[j].z - b[i].z) / (b[j].s - b[i].s);
    for (let j = 1; j + 1 < b.length; j++) {
      const v = b[j], hh = v.L / 2;
      if (hh <= 0 || s < v.s - hh || s > v.s + hh) continue;
      return gr(j - 1, j) + (gr(j, j + 1) - gr(j - 1, j)) * (s - (v.s - hh)) / v.L;
    }
    let i = 0; while (i + 2 < b.length && s > b[i + 1].s) i++;
    return gr(i, i + 1);
  }

  return { wrap, fres, fitG1, fitChain, point, head, curv, chainPose, sample, profile, profZ, profGrade };
})();
