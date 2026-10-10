// Headless checks for the prototype's geometry block: node prototypes/road-draw/test-geom.mjs
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const html = readFileSync(new URL('./index.html', import.meta.url), 'utf8');
const src = html.match(/<script id="geom">([\s\S]*?)<\/script>/)[1];
const ctx = {}; vm.createContext(ctx); vm.runInContext(src + '\nthis.G = G;', ctx);
const G = ctx.G;
let fails = 0;
const ok = (c, msg) => { if (!c) { fails++; console.error('FAIL', msg); } };
const near = (a, b, t, msg) => ok(Math.abs(a - b) <= t, `${msg}: ${a} vs ${b}`);

// Quarter circle.
let c = G.fitG1(50, 0, Math.PI / 2, 0, 50, Math.PI);
near(c.k0, 1 / 50, 1e-9, 'arc curvature'); near(c.dk, 0, 1e-12, 'arc dk'); near(c.L, 25 * Math.PI, 1e-6, 'arc length');

// Fuzz G1.
let seed = 1; const rnd = () => (seed = (seed * 16807) % 2147483647) / 2147483647;
for (let i = 0; i < 500; i++) {
  const x0 = rnd() * 1000 - 500, y0 = rnd() * 1000 - 500, x1 = rnd() * 1000 - 500, y1 = rnd() * 1000 - 500;
  const phi = Math.atan2(y1 - y0, x1 - x0), t0 = phi + (rnd() * 2 - 1) * 3, t1 = phi + (rnd() * 2 - 1) * 3;
  const f = G.fitG1(x0, y0, t0, x1, y1, t1);
  ok(f, 'g1 fit exists');
  const p = G.point(f, f.L);
  near(Math.hypot(p[0] - x1, p[1] - y1), 0, 1e-6 * (1 + Math.hypot(x1 - x0, y1 - y0)), 'g1 endpoint');
  near(G.wrap(G.head(f, f.L) - t1), 0, 1e-9, 'g1 heading');
}

// Chains: G2 + through points.
let g2 = 0;
for (let t = 0; t < 200; t++) {
  const P = [[0, 0]]; let h = 0;
  const n = 2 + Math.floor(rnd() * 8);
  for (let i = 1; i < n; i++) { h += (rnd() * 2 - 1) * 1.2; const d = 20 + rnd() * 180; P.push([P[i - 1][0] + Math.cos(h) * d, P[i - 1][1] + Math.sin(h) * d]); }
  const ch = G.fitChain(P);
  ok(ch, 'chain');
  if (ch.g2) g2++;
  ch.spans.forEach((s, i) => { const e = G.point(s, s.L); near(Math.hypot(e[0] - P[i + 1][0], e[1] - P[i + 1][1]), 0, 1e-5, 'chain through point'); });
}
ok(g2 >= 190, `G2 convergence ${g2}/200`);

// Vertical profile crest: +4% / -4%, K 10 -> L 80, e = A L / 8.
const b = G.profile([0, 200, 400], [0, 8, 0], 10, 10);
near(b[1].L, 80, 1e-9, 'vc length'); near(G.profZ(b, 200), 8 - 0.08 * 80 / 8, 1e-9, 'vc mid');
near(G.profZ(b, 160 - 1e-6), G.profZ(b, 160 + 1e-6), 1e-6, 'vc continuity');
near(G.profGrade(b, 200), 0, 1e-9, 'vc grade at crest');

console.log(fails ? `${fails} failures` : `all geometry checks passed (G2 ${g2}/200)`);
process.exit(fails ? 1 : 0);
