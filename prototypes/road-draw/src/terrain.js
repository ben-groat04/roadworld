// terrain.js: terrain generation, height sampling, terrain mesh, vertex colours, water.
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
const TG = Float32Array.from(TH);
const GR = new Uint8Array(TN * TN);       // 1 where grading changed the ground
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
// Current graded ground: exactly the rendered mesh surface (triangles a-b-d and a-d-c).
function hAt(x, y) { return hTri(TG, x, y); }
const hBase = (x, y) => hTri(TH, x, y);   // original ground as the (linear) mesh would show it
function hTri(A, x, y) {
  const fx = clamp(x / CELL, 0, TN - 1.0001), fy = clamp(y / CELL, 0, TN - 1.0001);
  const i = Math.floor(fx), j = Math.floor(fy), u = fx - i, v = fy - j, k = j * TN + i;
  const a = A[k], b = A[k + 1], c = A[k + TN], d = A[k + TN + 1];
  return u >= v ? a + u * (b - a) + v * (d - b) : a + u * (d - c) + v * (c - a);
}

// Terrain mesh (513 x 513, 4 m cells). three position = (x, h, -y).
const tGeo = new THREE.BufferGeometry();
const tPos = new Float32Array(TN * TN * 3), tNor = new Float32Array(TN * TN * 3), tCol = new Float32Array(TN * TN * 3);
for (let j = 0; j < TN; j++) for (let i = 0; i < TN; i++) { const k = (j * TN + i) * 3; tPos[k] = i * CELL; tPos[k + 1] = TG[j * TN + i]; tPos[k + 2] = -j * CELL; }
{
  const idx = new Uint32Array((TN - 1) * (TN - 1) * 6); let p = 0;
  for (let j = 0; j < TN - 1; j++) for (let i = 0; i < TN - 1; i++) {
    const a = j * TN + i, b = a + 1, c = a + TN, d = c + 1;
    idx[p++] = a; idx[p++] = b; idx[p++] = d; idx[p++] = a; idx[p++] = d; idx[p++] = c;
  }
  tGeo.setIndex(new THREE.BufferAttribute(idx, 1));
}
tGeo.setAttribute('position', new THREE.BufferAttribute(tPos, 3));
tGeo.setAttribute('normal', new THREE.BufferAttribute(tNor, 3));
tGeo.setAttribute('color', new THREE.BufferAttribute(tCol, 3));
tGeo.boundingSphere = new THREE.Sphere(new THREE.Vector3(WORLD / 2, 60, -WORLD / 2), WORLD);
const terrain = new THREE.Mesh(tGeo, new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, metalness: 0 }));
terrain.receiveShadow = true; terrain.castShadow = true; terrain.frustumCulled = false; scene.add(terrain);

const lerp3 = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
const GRASS_LO = [0.27, 0.43, 0.17], GRASS_HI = [0.5, 0.47, 0.26], ROCK = [0.47, 0.45, 0.42], EARTH = [0.42, 0.47, 0.22];
function shadeVertex(i, j) {
  const k = j * TN + i, il = Math.max(0, i - 1), ir = Math.min(TN - 1, i + 1), jd = Math.max(0, j - 1), ju = Math.min(TN - 1, j + 1);
  const sx = (TG[j * TN + ir] - TG[j * TN + il]) / ((ir - il) * CELL), sy = (TG[ju * TN + i] - TG[jd * TN + i]) / ((ju - jd) * CELL);
  const inv = 1 / Math.sqrt(sx * sx + sy * sy + 1);
  tNor[k * 3] = -sx * inv; tNor[k * 3 + 1] = inv; tNor[k * 3 + 2] = sy * inv;
  const h = TG[k], slope = Math.sqrt(sx * sx + sy * sy);
  let c = lerp3(GRASS_LO, GRASS_HI, clamp((h - 5) / 150, 0, 1));
  const rt = clamp((slope - 0.55) / 0.4, 0, 1); c = lerp3(c, ROCK, rt * rt * (3 - 2 * rt));
  if (GR[k]) c = lerp3(c, EARTH, 0.6);
  if (h < T.water + 1.2) c = lerp3(c, [0.36, 0.33, 0.24], clamp((T.water + 1.2 - h) / 3, 0, 0.8));
  const n = (((Math.imul(i, 73856093) ^ Math.imul(j, 19349663)) >>> 0) % 1000) / 1000 - 0.5, v = 1 + n * 0.1;
  tCol[k * 3] = c[0] * c[0] * v; tCol[k * 3 + 1] = c[1] * c[1] * v; tCol[k * 3 + 2] = c[2] * c[2] * v;   // approx sRGB -> linear
}
function updateTerrainMesh(rg) {
  const i0 = rg ? rg.i0 : 0, i1 = rg ? rg.i1 : TN - 1, j0 = rg ? rg.j0 : 0, j1 = rg ? rg.j1 : TN - 1;
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) tPos[(j * TN + i) * 3 + 1] = TG[j * TN + i];
  for (let j = Math.max(0, j0 - 1); j <= Math.min(TN - 1, j1 + 1); j++) for (let i = Math.max(0, i0 - 1); i <= Math.min(TN - 1, i1 + 1); i++) shadeVertex(i, j);
  tGeo.attributes.position.needsUpdate = tGeo.attributes.normal.needsUpdate = tGeo.attributes.color.needsUpdate = true;
}
updateTerrainMesh(null);

const water = new THREE.Mesh(new THREE.PlaneGeometry(WORLD, WORLD), new THREE.MeshStandardMaterial({ color: 0x2f7fa6, transparent: true, opacity: 0.68, roughness: 0.15, metalness: 0.1, depthWrite: false }));
water.rotation.x = -Math.PI / 2; water.position.set(WORLD / 2, T.water, -WORLD / 2); water.receiveShadow = true; scene.add(water);
const apron = new THREE.Mesh(new THREE.PlaneGeometry(40000, 40000), new THREE.MeshStandardMaterial({ color: 0x41522f, roughness: 1 }));
apron.rotation.x = -Math.PI / 2; apron.position.set(WORLD / 2, 0, -WORLD / 2); apron.receiveShadow = true; scene.add(apron);
