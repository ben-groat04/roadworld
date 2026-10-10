// terrain.js: base terrain generator, graded-ground height grid GH (shared with earthworks.js), clipmap ground rendering, water.
// ===== Terrain data (same generator as the 2D prototype). TH = original, TG = graded. =====
const TN = 513, CELL = WORLD / (TN - 1);
const TH = new Float32Array(TN * TN);
(function genTerrain() {
  let seed = 1337;
  const hash = (x, y) => { let h = (x * 374761393 + y * 668265263 + seed * 2246822519) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); return ((h ^ (h >>> 16)) >>> 0) / 4294967295; };
  const sm = t => t * t * (3 - 2 * t);
  const vnoise = (x, y) => { const xi = Math.floor(x), yi = Math.floor(y), xf = sm(x - xi), yf = sm(y - yi);
    const a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
    return a + (b - a) * xf + (c - a) * yf + (a - b - c + d) * xf * yf; };
  const fbm = (x, y) => { let s = 0, a = 0.5, f = 1; for (let o = 0; o < 5; o++) { s += a * vnoise(x * f, y * f); a *= 0.42; f *= 2.03; } return s / 0.84; };
  for (let j = 0; j < TN; j++) for (let i = 0; i < TN; i++) {
    const x = i * CELL, y = j * CELL;
    let h = 10 + 150 * Math.pow(fbm(x / 900, y / 900), 1.7);
    const vx = x - (y * 0.85 + 180 * Math.sin(y / 260));
    h -= 45 * Math.exp(-(vx - 300) * (vx - 300) / (2 * 140 * 140));
    h += 35 / (1 + Math.exp(-((y - x * 0.4) - 1250) / 25));
    TH[j * TN + i] = Math.max(2, h);
  }
})();
const TG = TH;   // compat alias: roads never modify the base heightfield
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
// Original ground (smooth Catmull-Rom, as in 2D): profile panel, cut/fill, wall edge heights.
function terrainH(x, y) {
  const fx = clamp(x / CELL, 0, TN - 1.001), fy = clamp(y / CELL, 0, TN - 1.001);
  const i = Math.floor(fx), j = Math.floor(fy), u = fx - i, v = fy - j;
  const at = (ii, jj) => TH[clamp(jj, 0, TN - 1) * TN + clamp(ii, 0, TN - 1)];
  const cr = (p0, p1, p2, p3, t) => p1 + 0.5 * t * (p2 - p0 + t * (2 * p0 - 5 * p1 + 4 * p2 - p3 + t * (3 * (p1 - p2) + p3 - p0)));
  const row = jj => cr(at(i - 1, jj), at(i, jj), at(i + 1, jj), at(i + 2, jj), u);
  return cr(row(j - 1), row(j), row(j + 1), row(j + 2), v);
}
// Triangle-interpolated height on a regular grid: triangles a-b-d and a-d-c.
function hTri(A, x, y) {
  const fx = clamp(x / CELL, 0, TN - 1.0001), fy = clamp(y / CELL, 0, TN - 1.0001);
  const i = Math.floor(fx), j = Math.floor(fy), u = fx - i, v = fy - j, k = j * TN + i;
  const a = A[k], b = A[k + 1], c = A[k + TN], d = A[k + TN + 1];
  return u >= v ? a + u * (b - a) + v * (d - b) : a + u * (d - c) + v * (c - a);
}


// ===== Graded ground grid GH (0.5 m). Declared here (terrain.js loads before earthworks.js); earthworks.js writes into GH and calls onGroundChanged(box). =====
const GRID_RES = 0.5, GN = Math.round(WORLD / GRID_RES) + 1;
const GH = new Float32Array(GN * GN);
(function initGH() {   // bicubic (Catmull-Rom, same as terrainH) upsample of TH, separable
  const R = Math.round(CELL / GRID_RES), W = [];
  for (let p = 0; p < R; p++) { const t = p / R, f = (a, b, c, d) => b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a))); W.push([f(1, 0, 0, 0), f(0, 1, 0, 0), f(0, 0, 1, 0), f(0, 0, 0, 1)]); }
  const tmp = new Float32Array(TN * GN);
  for (let j = 0; j < TN; j++) for (let k = 0; k < GN; k++) {
    const i = Math.floor(k / R), w = W[k % R], r = j * TN, i0 = Math.max(0, i - 1), i2 = Math.min(TN - 1, i + 1), i3 = Math.min(TN - 1, i + 2);
    tmp[j * GN + k] = w[0] * TH[r + i0] + w[1] * TH[r + i] + w[2] * TH[r + i2] + w[3] * TH[r + i3];
  }
  for (let y = 0; y < GN; y++) {
    const j = Math.floor(y / R), w = W[y % R], j0 = Math.max(0, j - 1) * GN, j1 = j * GN, j2 = Math.min(TN - 1, j + 1) * GN, j3 = Math.min(TN - 1, j + 2) * GN;
    for (let k = 0; k < GN; k++) GH[y * GN + k] = w[0] * tmp[j0 + k] + w[1] * tmp[j1 + k] + w[2] * tmp[j2 + k] + w[3] * tmp[j3 + k];
  }
})();
// Graded ground height at world (x, y): triangles a-b-d and a-d-c per 0.5 m cell (matches the finest mesh ring).
function groundAt(x, y) {
  const fx = clamp(x / GRID_RES, 0, GN - 1.0001), fy = clamp(y / GRID_RES, 0, GN - 1.0001);
  const i = Math.floor(fx), j = Math.floor(fy), u = fx - i, v = fy - j, k = j * GN + i;
  const a = GH[k], b = GH[k + 1], c = GH[k + GN], d = GH[k + GN + 1];
  return u >= v ? a + u * (b - a) + v * (d - b) : a + u * (d - c) + v * (c - a);
}
// Bicubic base ground at 0.5 m (the exact function GH is initialised from); earthworks grades against this.
function hBaseFine(x, y) { return terrainH(x, y); }
const hAt = groundAt;   // shim: everything that used the old base-terrain height now sees the graded ground
function hBase(x, y) { return hTri(TH, x, y); }   // original ground as the (linear 4 m) mesh would show it

// ===== Textures: GH (R32F, nearest + manual bilinear), base TH (R32F) =====
const hTex = new THREE.DataTexture(GH, GN, GN, THREE.RedFormat, THREE.FloatType);
hTex.minFilter = hTex.magFilter = THREE.NearestFilter; hTex.generateMipmaps = false; hTex.flipY = false; hTex.unpackAlignment = 4; hTex.needsUpdate = true;
const tbTex = new THREE.DataTexture(TH, TN, TN, THREE.RedFormat, THREE.FloatType);
tbTex.minFilter = tbTex.magFilter = THREE.NearestFilter; tbTex.generateMipmaps = false; tbTex.needsUpdate = true;
let hTexLive = false;   // true once the first full upload has been issued by a draw
// box: null (everything) or { i0, j0, i1, j1 } in grid indices (inclusive), or { x0, y0, x1, y1 } / { minX, minY, maxX, maxY } in metres.
function onGroundChanged(box) {
  needsRender = true;
  if (!hTexLive) return;   // first full upload still pending
  let i0 = 0, j0 = 0, i1 = GN - 1, j1 = GN - 1;
  if (box) {
    if (box.i0 != null) { i0 = box.i0; j0 = box.j0; i1 = box.i1; j1 = box.j1; }
    else {
      const x0 = box.x0 ?? box.minX, y0 = box.y0 ?? box.minY, x1 = box.x1 ?? box.maxX, y1 = box.y1 ?? box.maxY;
      if (x0 != null) { i0 = Math.floor(x0 / GRID_RES) - 1; j0 = Math.floor(y0 / GRID_RES) - 1; i1 = Math.ceil(x1 / GRID_RES) + 1; j1 = Math.ceil(y1 / GRID_RES) + 1; }
    }
  }
  i0 = clamp(Math.floor(i0), 0, GN - 1); j0 = clamp(Math.floor(j0), 0, GN - 1); i1 = clamp(Math.ceil(i1), i0, GN - 1); j1 = clamp(Math.ceil(j1), j0, GN - 1);
  if (i1 < i0 || j1 < j0) return;
  if ((i1 - i0 + 1) * (j1 - j0 + 1) > GN * GN * 0.5) { hTex.needsUpdate = true; return; }
  const gl = renderer.getContext(), tex = renderer.properties.get(hTex).__webglTexture;
  if (!tex) { hTex.needsUpdate = true; return; }
  const prev = gl.getParameter(gl.TEXTURE_BINDING_2D);   // upload just the changed rectangle straight from GH (row stride GN)
  gl.bindTexture(gl.TEXTURE_2D, tex); gl.pixelStorei(gl.UNPACK_ROW_LENGTH, GN); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, i0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, j0);
  gl.texSubImage2D(gl.TEXTURE_2D, 0, i0, j0, i1 - i0 + 1, j1 - j0 + 1, gl.RED, gl.FLOAT, GH);
  gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0); gl.pixelStorei(gl.UNPACK_SKIP_PIXELS, 0); gl.pixelStorei(gl.UNPACK_SKIP_ROWS, 0); gl.bindTexture(gl.TEXTURE_2D, prev);
}

// ===== Ground clipmap: concentric rings at 0.5, 1, 2, 4, 8, 16 m, all centred on one point snapped to 16 m so every ring's grid lines up exactly =====
const RING_N = 256, RING_SP = [0.5, 1, 2, 4, 8, 16];
const GLSL_H = `
const float G_RES = ${GRID_RES.toFixed(3)};
const float G_WORLD = ${WORLD.toFixed(1)};
uniform highp sampler2D uH;
float Hi(ivec2 p) { return texelFetch(uH, clamp(p, ivec2(0), ivec2(${GN - 1})), 0).r; }
float Hg(vec2 w) {   // bilinear graded height at world (x, y)
  vec2 g = clamp(w, 0.0, G_WORLD) / G_RES; ivec2 i = ivec2(floor(g)); vec2 f = g - vec2(i);
  return mix(mix(Hi(i), Hi(i + ivec2(1, 0)), f.x), mix(Hi(i + ivec2(0, 1)), Hi(i + ivec2(1, 1)), f.x), f.y);
}`;
const ringU = { uH: { value: hTex }, uTB: { value: tbTex }, uCentre: { value: new THREE.Vector2() }, uWater: { value: T.water } };
const ringIdx = (full) => {
  const n = RING_N, h = n / 2, q = n / 4, out = []; const V = (i, j) => (j + h) * (n + 1) + (i + h);
  for (let j = -h; j < h; j++) for (let i = -h; i < h; i++) {
    if (!full && i >= -q && i < q && j >= -q && j < q) continue;
    const a = V(i, j), b = V(i + 1, j), c = V(i, j + 1), d = V(i + 1, j + 1); out.push(a, b, d, a, d, c);
  }
  return new THREE.BufferAttribute(new Uint32Array(out), 1);
};
const ringGeo = (full) => {
  const n = RING_N, h = n / 2, pos = new Float32Array((n + 1) * (n + 1) * 3);
  for (let j = 0; j <= n; j++) for (let i = 0; i <= n; i++) { const k = (j * (n + 1) + i) * 3; pos[k] = i - h; pos[k + 2] = j - h; }
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(pos, 3)); g.setIndex(ringIdx(full));
  g.boundingSphere = new THREE.Sphere(new THREE.Vector3(WORLD / 2, 60, -WORLD / 2), WORLD * 2); g.boundingBox = new THREE.Box3(new THREE.Vector3(-1e5, -100, -1e5), new THREE.Vector3(1e5, 400, 1e5));
  return g;
};
const GEO_FULL = ringGeo(true), GEO_HOLE = ringGeo(false);
function makeRingMat(sp) {
  const m = new THREE.MeshStandardMaterial({ color: 0xffffff, roughness: 0.95, metalness: 0 });
  m.customProgramCacheKey = () => 'groundring';
  m.onBeforeCompile = sh => {
    Object.assign(sh.uniforms, ringU); sh.uniforms.uSp = { value: sp }; sh.uniforms.uN = { value: RING_N };
    sh.vertexShader = sh.vertexShader.replace('#include <common>', `#include <common>
${GLSL_H}
uniform highp sampler2D uTB; uniform vec2 uCentre; uniform float uSp, uN; varying vec3 vW; varying float vDelta;
float crm(float a, float b, float c, float d, float t) { return b + 0.5 * t * (c - a + t * (2.0 * a - 5.0 * b + 4.0 * c - d + t * (3.0 * (b - c) + d - a))); }
float Bi(int x, int y) { return texelFetch(uTB, clamp(ivec2(x, y), ivec2(0), ivec2(${TN - 1})), 0).r; }
float baseH(vec2 w) {   // Catmull-Rom of the 4 m base terrain (same as terrainH on the CPU)
  vec2 f = clamp(w / ${CELL.toFixed(1)}, 0.0, ${TN - 1}.0 - 0.001); ivec2 i = ivec2(floor(f)); vec2 u = f - vec2(i);
  float r0 = crm(Bi(i.x - 1, i.y - 1), Bi(i.x, i.y - 1), Bi(i.x + 1, i.y - 1), Bi(i.x + 2, i.y - 1), u.x);
  float r1 = crm(Bi(i.x - 1, i.y), Bi(i.x, i.y), Bi(i.x + 1, i.y), Bi(i.x + 2, i.y), u.x);
  float r2 = crm(Bi(i.x - 1, i.y + 1), Bi(i.x, i.y + 1), Bi(i.x + 1, i.y + 1), Bi(i.x + 2, i.y + 1), u.x);
  float r3 = crm(Bi(i.x - 1, i.y + 2), Bi(i.x, i.y + 2), Bi(i.x + 1, i.y + 2), Bi(i.x + 2, i.y + 2), u.x);
  return crm(r0, r1, r2, r3, u.y);
}`).replace('#include <beginnormal_vertex>', `
vec2 lp = position.xz;
vec2 wp = clamp(uCentre + lp * uSp, 0.0, G_WORLD);
float gh = Hg(wp);
{ // crack-free seam: odd vertices on the outer edge take the average of their even neighbours (what the next coarser ring interpolates)
  float hn = uN * 0.5 - 0.5; vec2 s = vec2(0.0);
  if (abs(lp.x) > hn && mod(abs(lp.y), 2.0) > 0.5) s = vec2(0.0, uSp); else if (abs(lp.y) > hn && mod(abs(lp.x), 2.0) > 0.5) s = vec2(uSp, 0.0);
  if (s.x + s.y > 0.0) gh = 0.5 * (Hg(wp - s) + Hg(wp + s));
}
float ne = max(uSp, G_RES);
vec3 objectNormal = normalize(vec3(-(Hg(wp + vec2(ne, 0.0)) - Hg(wp - vec2(ne, 0.0))), 2.0 * ne, Hg(wp + vec2(0.0, ne)) - Hg(wp - vec2(0.0, ne))));
#ifdef USE_TANGENT
vec3 objectTangent = vec3(tangent.xyz);
#endif`).replace('#include <begin_vertex>', `vec3 transformed = vec3(wp.x, gh, -wp.y); vW = transformed; vDelta = gh - baseH(wp);`)
      .replace('#include <project_vertex>', `vec4 mvPosition = viewMatrix * vec4(transformed, 1.0); gl_Position = projectionMatrix * mvPosition;`);
    sh.fragmentShader = sh.fragmentShader.replace('#include <common>', `#include <common>
${GLSL_H}
uniform float uSp, uWater; varying vec3 vW; varying float vDelta;
float hsh(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453) - 0.5; }
float vnz(vec2 p) { vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f); return mix(mix(hsh(i), hsh(i + vec2(1, 0)), f.x), mix(hsh(i + vec2(0, 1)), hsh(i + vec2(1, 1)), f.x), f.y); }`)
      .replace('#include <color_fragment>', `
vec3 wp3 = vW; float hb = vW.y - vDelta;
float gr = smoothstep(0.2, 1.5, abs(vDelta));
float slp = length(gNormal.xz) / max(gNormal.y, 0.05);
// world-space detail: 3 scales of value noise (xz) + triplanar blend for steep faces (2 extra lookups each of the mid scale)
vec3 tw = pow(abs(gNormal), vec3(4.0)); tw /= (tw.x + tw.y + tw.z);
float nA = vnz(wp3.xz / 3.0) + 0.6 * vnz(wp3.xz / 0.9);
float nB = vnz(wp3.zy / 2.5) + 0.6 * vnz(wp3.zy / 0.8), nC = vnz(wp3.xy / 2.5) + 0.6 * vnz(wp3.xy / 0.8);
float kd = tw.y * nA + tw.x * nB + tw.z * nC;
float macro = vnz(wp3.xz / 220.0) + 0.5 * vnz(wp3.xz / 67.0);
float dry = smoothstep(0.1, 0.5, vnz(wp3.xz / 140.0 + 17.0));
vec3 gcol = mix(vec3(0.27, 0.43, 0.17), vec3(0.5, 0.47, 0.26), clamp((hb - 5.0) / 150.0, 0.0, 1.0));
gcol = mix(gcol, vec3(0.42, 0.42, 0.20), dry * 0.35);
gcol *= 1.0 + 0.22 * macro;
gcol = mix(gcol, vec3(0.36, 0.33, 0.24), clamp((uWater + 1.2 - hb) / 3.0, 0.0, 0.8) * (1.0 - gr));
gcol = mix(gcol, vec3(0.30, 0.50, 0.16), gr * 0.3);
// subtle darker toe where a slope meets flatter ground (slope falls off with the 3x3 kernel); cheap proxy: damp-ish darkening on gentle but non-flat ground
gcol *= 1.0 - 0.18 * smoothstep(0.08, 0.3, slp) * (1.0 - smoothstep(0.3, 0.6, slp));
// exposed soil / rock: steep (> ~35 deg => slp > 0.7) or freshly cut (cut depth > 2 m)
float steep = smoothstep(0.62, 0.85, slp);
float cut = smoothstep(1.5, 3.0, -vDelta) * gr;
float ex = max(steep, cut);
vec3 soil = mix(vec3(0.40, 0.31, 0.22), vec3(0.47, 0.45, 0.42), smoothstep(-0.4, 0.5, nB + nC + macro));
soil *= 1.0 + 0.25 * kd;
gcol = mix(gcol, soil, ex);
float kn = 1.0 + 0.1 * kd * (1.0 - ex);
diffuseColor.rgb = gcol * gcol * kn;`)
      .replace('void main() {', `void main() {
  vec2 gw = vec2(vW.x, -vW.z); float ge = max(uSp, 1.5);   // wide 3x3 Sobel-weighted kernel on bilinear height: smooth normals
  float gdx = 0.0, gdy = 0.0;
  for (int a = -1; a <= 1; a++) { float wgt = a == 0 ? 2.0 : 1.0; float o = float(a) * ge;
    gdx += wgt * (Hg(gw + vec2(ge, o)) - Hg(gw + vec2(-ge, o)));
    gdy += wgt * (Hg(gw + vec2(o, ge)) - Hg(gw + vec2(o, -ge))); }
  vec3 gNormal = normalize(vec3(-gdx / 4.0, 2.0 * ge, gdy / 4.0));`)
      .replace('#include <normal_fragment_begin>', `vec3 normal = normalize((viewMatrix * vec4(gNormal, 0.0)).xyz); vec3 nonPerturbedNormal = normal;`)
      .replace('#include <fog_fragment>', `#include <fog_fragment>
#ifdef USE_FOG
{ vec2 ed = min(vW.xz * vec2(1.0, -1.0), G_WORLD - vW.xz * vec2(1.0, -1.0)); float de = min(ed.x, ed.y); gl_FragColor.rgb = mix(fogColor, gl_FragColor.rgb, smoothstep(0.0, 500.0, de)); }
#endif`);
  };
  return m;
}
const terrain = new THREE.Group(); terrain.userData.kind = 'terrain';
const rings = RING_SP.map((sp, k) => {
  const m = new THREE.Mesh(k === 0 ? GEO_FULL : GEO_HOLE, makeRingMat(sp));
  m.userData.kind = 'terrain'; m.receiveShadow = true; m.castShadow = false; m.renderOrder = 5; m.frustumCulled = false; m.matrixAutoUpdate = false;
  if (k === 0) m.onBeforeRender = () => { hTexLive = true; };
  terrain.add(m); return m;
});
scene.add(terrain);
// Clipmap re-centre: only moves the shared uCentre uniform (snapped to 16 m so all rings stay aligned). Geometry is never rebuilt; vertices are offset on the GPU.
function updateClipmap() {
  const cx = Math.round(camera.position.x / 16) * 16, cy = Math.round(-camera.position.z / 16) * 16, c = ringU.uCentre.value;
  if (c.x !== cx || c.y !== cy) c.set(cx, cy);
}

const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const GRASS_LO = [0.27, 0.43, 0.17], GRASS_HI = [0.5, 0.47, 0.26], ROCK = [0.47, 0.45, 0.42];
const hash2 = (i, j) => (((Math.imul(i, 73856093) ^ Math.imul(j, 19349663)) >>> 0) % 1000) / 1000 - 0.5;
// The terrain colour function: linear RGB at world (x, y) from base ground height / slope (+ bilinear speckle). Terrain vertices and road earthworks both use it.
function terrainColour(x, y, out) {
  const h = hBase(x, y), sx = (hBase(x + CELL, y) - hBase(x - CELL, y)) / (2 * CELL), sy = (hBase(x, y + CELL) - hBase(x, y - CELL)) / (2 * CELL);
  const slope = Math.sqrt(sx * sx + sy * sy);
  let c = lerp3(GRASS_LO, GRASS_HI, clamp((h - 5) / 150, 0, 1));
  const rt = clamp((slope - 0.55) / 0.4, 0, 1); c = lerp3(c, ROCK, rt * rt * (3 - 2 * rt));
  if (h < T.water + 1.2) c = lerp3(c, [0.36, 0.33, 0.24], clamp((T.water + 1.2 - h) / 3, 0, 0.8));
  const fx = x / CELL, fy = y / CELL, i = Math.floor(fx), j = Math.floor(fy), u = fx - i, v = fy - j;
  const n = hash2(i, j) * (1 - u) * (1 - v) + hash2(i + 1, j) * u * (1 - v) + hash2(i, j + 1) * (1 - u) * v + hash2(i + 1, j + 1) * u * v, k = 1 + n * 0.1;
  out[0] = c[0] * c[0] * k; out[1] = c[1] * c[1] * k; out[2] = c[2] * c[2] * k;   // approx sRGB -> linear
}
function updateTerrainMesh() {}   // ground is drawn straight from GH; earthworks.js calls onGroundChanged(box)
const maskCanvas = document.createElement('canvas');   // shim: the corridor mask is gone (ui.js still lists it in the debug export)

// Water: only draws where the graded ground (height texture) is below the water level.
const waterMat = new THREE.MeshStandardMaterial({ color: 0x2f7fa6, transparent: true, opacity: 0.88, roughness: 0.15, metalness: 0.1, depthWrite: true, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
waterMat.onBeforeCompile = shader => {
  shader.uniforms.uH = { value: hTex }; shader.uniforms.uLevel = { value: T.water }; waterMat.userData.sh = shader;
  shader.vertexShader = shader.vertexShader.replace('#include <common>', '#include <common>\nvarying vec2 vWXZ;').replace('#include <begin_vertex>', '#include <begin_vertex>\nvWXZ = (modelMatrix * vec4(transformed, 1.0)).xz;');
  shader.fragmentShader = shader.fragmentShader.replace('#include <common>', '#include <common>\nvarying vec2 vWXZ;\nuniform float uLevel;\nfloat wDepth;\n' + GLSL_H)
    .replace('void main() {', `void main() {
  wDepth = uLevel - Hg(vec2(vWXZ.x, -vWXZ.y));
  if (wDepth < -0.2) discard;`)
    .replace('#include <color_fragment>', `#include <color_fragment>
{ float d = max(wDepth, 0.0);
  vec3 shallow = vec3(0.30, 0.55, 0.50), deep = vec3(0.05, 0.22, 0.34), bank = vec3(0.10, 0.09, 0.06);
  vec3 wc = mix(shallow, deep, smoothstep(0.1, 5.0, d));
  wc = mix(bank, wc, smoothstep(0.0, 0.5, d));   // damp dark bank band at the shoreline
  diffuseColor.rgb = wc * wc;
  diffuseColor.a = mix(0.97, 0.9, smoothstep(0.1, 2.0, d)) * smoothstep(-0.2, 0.15, wDepth); }`)
    .replace('#include <opaque_fragment>', `{ vec3 vd = normalize(vViewPosition); vec3 nn = normalize(normal); float sp = pow(max(dot(nn, normalize(vd + normalize((viewMatrix * vec4(0.4, 0.8, 0.3, 0.0)).xyz))), 0.0), 120.0);
  outgoingLight += vec3(sp * 0.6); }
#include <opaque_fragment>`);
};
const water = new THREE.Mesh(new THREE.PlaneGeometry(WORLD, WORLD), waterMat);
water.userData.kind = 'water'; water.rotation.x = -Math.PI / 2; water.position.set(WORLD / 2, T.water, -WORLD / 2); water.receiveShadow = true; scene.add(water);
water.onBeforeRender = () => { if (waterMat.userData.sh) waterMat.userData.sh.uniforms.uLevel.value = water.position.y; };
// Skirt around the world edge down below the lowest height, dark earth.
{
  const N = TN, pos = [], idx = [], BOT = -40;
  const edge = (f) => { const base = pos.length / 3; for (let k = 0; k < N; k++) { const [x, y, h] = f(k); pos.push(x, h, -y, x, BOT, -y); } for (let k = 0; k < N - 1; k++) { const a = base + k * 2; idx.push(a, a + 1, a + 2, a + 2, a + 1, a + 3); } };
  edge(k => [k * CELL, 0, TH[k]]); edge(k => [k * CELL, WORLD, TH[(N - 1) * N + k]]); edge(k => [0, k * CELL, TH[k * N]]); edge(k => [WORLD, k * CELL, TH[k * N + N - 1]]);
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); g.setIndex(idx); g.computeVertexNormals();
  const sk = new THREE.Mesh(g, new THREE.MeshBasicMaterial({ color: scene.background.clone(), side: THREE.DoubleSide, fog: false }));
  sk.userData.kind = 'skirt'; sk.frustumCulled = false; sk.receiveShadow = true; sk.renderOrder = 6; scene.add(sk);
}
const apron = new THREE.Mesh(new THREE.PlaneGeometry(40000, 40000), new THREE.MeshStandardMaterial({ color: 0x41522f, roughness: 1 }));
apron.rotation.x = -Math.PI / 2; apron.position.set(WORLD / 2, 0, -WORLD / 2); apron.receiveShadow = true; apron.renderOrder = 6; scene.add(apron);
