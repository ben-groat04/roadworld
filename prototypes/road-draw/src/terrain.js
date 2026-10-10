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
// Ground as rendered by the (linear) mesh: triangles a-b-d and a-d-c.
function hAt(x, y) { return hTri(TH, x, y); }
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
for (let j = 0; j < TN; j++) for (let i = 0; i < TN; i++) { const k = (j * TN + i) * 3; tPos[k] = i * CELL; tPos[k + 1] = TH[j * TN + i]; tPos[k + 2] = -j * CELL; }
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

// Corridor mask: 1 px = 1 m over the world, white where a road's earthwork replaces the terrain. Fragments with mask > 0.5 are discarded.
const maskCanvas = document.createElement('canvas'); maskCanvas.width = maskCanvas.height = WORLD;
const maskCtx = maskCanvas.getContext('2d');
maskCtx.fillStyle = '#000'; maskCtx.fillRect(0, 0, WORLD, WORLD);
const maskTex = new THREE.CanvasTexture(maskCanvas);
maskTex.flipY = false; maskTex.generateMipmaps = false; maskTex.minFilter = maskTex.magFilter = THREE.LinearFilter;
maskTex.wrapS = maskTex.wrapT = THREE.ClampToEdgeWrapping;
function addMaskDiscard(shader) {
  shader.uniforms.uMask = { value: maskTex };
  shader.vertexShader = shader.vertexShader.replace('#include <common>', '#include <common>\nvarying vec2 vMaskXZ;')
    .replace('#include <begin_vertex>', '#include <begin_vertex>\nvMaskXZ = (modelMatrix * vec4(transformed, 1.0)).xz;');
  // Conservative, smooth discard: erode the (linearly filtered, antialiased) mask by ~1 m using 4 diagonal taps, so the discard edge sits well inside the earthwork edge and has no 1 m stair-steps.
  shader.fragmentShader = shader.fragmentShader.replace('#include <common>', `#include <common>\nvarying vec2 vMaskXZ;\nuniform sampler2D uMask;`)
    .replace('void main() {', `void main() {
  { vec2 mq = vec2(vMaskXZ.x, -vMaskXZ.y), e = vec2(1.0, 1.0); const float MW = ${WORLD.toFixed(1)};
    float mm = min(min(texture2D(uMask, (mq + e * vec2(1.0, 1.0)) / MW).r, texture2D(uMask, (mq + e * vec2(-1.0, 1.0)) / MW).r),
                   min(texture2D(uMask, (mq + e * vec2(1.0, -1.0)) / MW).r, texture2D(uMask, (mq + e * vec2(-1.0, -1.0)) / MW).r));
    mm = min(mm, texture2D(uMask, mq / MW).r);
    if (mm > 0.55) discard; }`);
}
// Terrain material (masked) and the identical unmasked one used by road earthworks.
const groundMat = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.95, metalness: 0 });
const terrainMat = groundMat.clone(); terrainMat.onBeforeCompile = addMaskDiscard;
const terrain = new THREE.Mesh(tGeo, terrainMat);
terrain.customDepthMaterial = new THREE.MeshDepthMaterial({ depthPacking: THREE.RGBADepthPacking }); terrain.customDepthMaterial.onBeforeCompile = addMaskDiscard;
terrain.receiveShadow = true; terrain.castShadow = true; terrain.frustumCulled = false; scene.add(terrain);

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
{
  const o = [0, 0, 0];
  for (let j = 0; j < TN; j++) for (let i = 0; i < TN; i++) {
    const k = j * TN + i, il = Math.max(0, i - 1), ir = Math.min(TN - 1, i + 1), jd = Math.max(0, j - 1), ju = Math.min(TN - 1, j + 1);
    const sx = (TH[j * TN + ir] - TH[j * TN + il]) / ((ir - il) * CELL), sy = (TH[ju * TN + i] - TH[jd * TN + i]) / ((ju - jd) * CELL), inv = 1 / Math.sqrt(sx * sx + sy * sy + 1);
    tNor[k * 3] = -sx * inv; tNor[k * 3 + 1] = inv; tNor[k * 3 + 2] = sy * inv;
    terrainColour(i * CELL, j * CELL, o); tCol[k * 3] = o[0]; tCol[k * 3 + 1] = o[1]; tCol[k * 3 + 2] = o[2];
  }
}
function updateTerrainMesh() {}   // base terrain is static (kept for callers)

// Height texture (float, nearest + manual bilinear in the shader) so water only draws where the base terrain is below the water level.
const hTex = new THREE.DataTexture(TH, TN, TN, THREE.RedFormat, THREE.FloatType);
hTex.minFilter = hTex.magFilter = THREE.NearestFilter; hTex.generateMipmaps = false; hTex.needsUpdate = true;
const waterMat = new THREE.MeshStandardMaterial({ color: 0x2f7fa6, transparent: true, opacity: 0.88, roughness: 0.15, metalness: 0.1, depthWrite: true, polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2 });
waterMat.onBeforeCompile = shader => {
  shader.uniforms.uH = { value: hTex }; shader.uniforms.uLevel = { value: T.water }; waterMat.userData.sh = shader;
  shader.vertexShader = shader.vertexShader.replace('#include <common>', '#include <common>\nvarying vec2 vWXZ;').replace('#include <begin_vertex>', '#include <begin_vertex>\nvWXZ = (modelMatrix * vec4(transformed, 1.0)).xz;');
  shader.fragmentShader = shader.fragmentShader.replace('#include <common>', '#include <common>\nvarying vec2 vWXZ;\nuniform sampler2D uH;\nuniform float uLevel;')
    .replace('void main() {', `void main() {
  { vec2 g = vec2(vWXZ.x, -vWXZ.y) / ${WORLD.toFixed(1)} * ${(TN - 1).toFixed(1)}; vec2 f = fract(g); vec2 i0 = floor(g) + 0.5; const float N = ${TN.toFixed(1)};
    float a = texture2D(uH, i0 / N).r, b = texture2D(uH, (i0 + vec2(1.0, 0.0)) / N).r, c = texture2D(uH, (i0 + vec2(0.0, 1.0)) / N).r, d = texture2D(uH, (i0 + 1.0) / N).r;
    float h = mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
    if (h > uLevel + 0.2) discard; }`);
};
const water = new THREE.Mesh(new THREE.PlaneGeometry(WORLD, WORLD), waterMat);
water.rotation.x = -Math.PI / 2; water.position.set(WORLD / 2, T.water, -WORLD / 2); water.receiveShadow = true; scene.add(water);
water.onBeforeRender = () => { if (waterMat.userData.sh) waterMat.userData.sh.uniforms.uLevel.value = water.position.y; };
// Skirt around the world edge down below the lowest height, dark earth.
{
  const N = TN, pos = [], idx = [], BOT = -40;
  const edge = (f) => { const base = pos.length / 3; for (let k = 0; k < N; k++) { const [x, y, h] = f(k); pos.push(x, h, -y, x, BOT, -y); } for (let k = 0; k < N - 1; k++) { const a = base + k * 2; idx.push(a, a + 1, a + 2, a + 2, a + 1, a + 3); } };
  edge(k => [k * CELL, 0, TH[k]]); edge(k => [k * CELL, WORLD, TH[(N - 1) * N + k]]); edge(k => [0, k * CELL, TH[k * N]]); edge(k => [WORLD, k * CELL, TH[k * N + N - 1]]);
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3)); g.setIndex(idx); g.computeVertexNormals();
  const sk = new THREE.Mesh(g, new THREE.MeshStandardMaterial({ color: 0x2a2118, roughness: 1, side: THREE.DoubleSide }));
  sk.frustumCulled = false; sk.receiveShadow = true; scene.add(sk);
}
const apron = new THREE.Mesh(new THREE.PlaneGeometry(40000, 40000), new THREE.MeshStandardMaterial({ color: 0x41522f, roughness: 1 }));
apron.rotation.x = -Math.PI / 2; apron.position.set(WORLD / 2, 0, -WORLD / 2); apron.receiveShadow = true; scene.add(apron);
