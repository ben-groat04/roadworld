// Standalone viewer for sim_core export_mesh output. Sim frame: x east, y north, z up (right-handed).
// three.js frame: x east, y up, z = -north. (x, y, z)sim -> (x, z, -y)three is a proper rotation (det +1),
// so nothing is mirrored and triangle winding is preserved.
import * as THREE from 'three';
import { Sky } from 'three/addons/objects/Sky.js';

const S2T = (x, y, z) => [x, z, -y];

// surface class -> texture slot (see unreal/.../PolyHaven/manifest.json). Unlisted classes get a plain grey.
const SLOT = { asphalt: 'asphalt_new', paving: 'pavement', kerb: 'kerb_concrete', island: 'pavement', grass: 'grass',
  structural_concrete: 'structural_concrete', parapet: 'structural_concrete', tunnel_lining: 'structural_concrete', coping: 'kerb_concrete', wall_facing: 'wall_brick' };
const MARK_COLOURS = { white: 0xe8e6de, yellow: 0xe3b81a };

function parseObj(text) {
  const v = [], vt = [], vn = [], idx = [];
  for (const line of text.split('\n')) {
    const c = line.charCodeAt(0);
    if (c === 118) { // v
      const p = line.split(' ');
      if (p[0] === 'v') v.push(+p[1], +p[2], +p[3]);
      else if (p[0] === 'vt') vt.push(+p[1], +p[2]);
      else if (p[0] === 'vn') vn.push(+p[1], +p[2], +p[3]);
    } else if (c === 102) { // f a/b/c ...
      const p = line.trim().split(/\s+/).slice(1).map(s => +s.split('/')[0] - 1);
      for (let i = 1; i + 1 < p.length; i++) idx.push(p[0], p[i], p[i + 1]);
    }
  }
  const n = v.length / 3;
  const pos = new Float32Array(n * 3), nor = new Float32Array(n * 3), uv = new Float32Array(n * 2);
  for (let i = 0; i < n; i++) {
    const q = S2T(v[3 * i], v[3 * i + 1], v[3 * i + 2]); pos.set(q, 3 * i);
    if (vn.length) nor.set(S2T(vn[3 * i], vn[3 * i + 1], vn[3 * i + 2]), 3 * i);
    if (vt.length) uv.set([vt[2 * i], vt[2 * i + 1]], 2 * i);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  g.setIndex(idx);
  g.computeBoundingSphere();
  return g;
}

let W = 1600, H = 900;
const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
renderer.setPixelRatio(1);
renderer.setSize(W, H);
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 0.75;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
document.body.appendChild(renderer.domElement);
const maxAniso = renderer.capabilities.getMaxAnisotropy();

const scene = new THREE.Scene();
const camera = new THREE.PerspectiveCamera(50, W / H, 0.2, 6000);

// Sky + image-based ambient from it.
const sky = new Sky(); sky.scale.setScalar(20000); scene.add(sky);
const sunDir0 = new THREE.Vector3().setFromSphericalCoords(1, THREE.MathUtils.degToRad(90 - 38), THREE.MathUtils.degToRad(215)); // mid-afternoon, from the south-west-ish
{
  const u = sky.material.uniforms;
  u.turbidity.value = 3; u.rayleigh.value = 1.2; u.mieCoefficient.value = 0.004; u.mieDirectionalG.value = 0.8;
  u.sunPosition.value.copy(sunDir0);
  const pm = new THREE.PMREMGenerator(renderer);
  const envScene = new THREE.Scene(); envScene.add(sky.clone());
  scene.environment = pm.fromScene(envScene, 0, 1, 20000).texture;
  scene.environmentIntensity = 0.55;
}
const sunDir = sunDir0.clone(); // per-view override: view.sun = [compass azimuth deg (0 = north, 90 = east), elevation deg]
scene.fog = new THREE.Fog(0xb9c7d6, 600, 4500);
const sun = new THREE.DirectionalLight(0xfff1dc, 3.2);
sun.castShadow = true;
sun.shadow.mapSize.set(4096, 4096);
sun.shadow.bias = -0.0004; sun.shadow.normalBias = 0.05;
scene.add(sun, sun.target);

const loader = new THREE.TextureLoader();
const loadTex = (url, srgb) => new Promise((res, rej) => loader.load(url, t => {
  t.wrapS = t.wrapT = THREE.RepeatWrapping; t.anisotropy = maxAniso;
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  res(t);
}, undefined, () => rej(new Error('texture failed: ' + url))));

async function slotMaterial(man, slot) {
  const s = man.slots[slot], f = s.files, base = '/tex/';
  const [d, n, r, a] = await Promise.all([loadTex(base + f.diff, true), loadTex(base + f.nor_dx, false), loadTex(base + f.rough, false), loadTex(base + f.ao, false)]);
  const k = 1 / s.size_m; // mesh UVs are metres
  for (const t of [d, n, r, a]) t.repeat.set(k, k);
  return new THREE.MeshStandardMaterial({
    map: d, normalMap: n, normalScale: new THREE.Vector2(1, -1), // files are DirectX normals: flip green
    roughnessMap: r, roughness: 1, aoMap: a, aoMapIntensity: 1, metalness: 0,
  });
}

const exportInfo = await (await fetch('/export/export.json')).json();
const man = await (await fetch('/tex/manifest.json')).json();
const off = exportInfo.origin_offset; // sim = export + off
const mats = {};
const group = new THREE.Group(); scene.add(group);
const report = [];
for (const f of exportInfo.files) {
  const geo = parseObj(await (await fetch('/export/' + f.file)).text());
  let mat;
  if (f.kind === 'marking') {
    mat = new THREE.MeshStandardMaterial({
      color: MARK_COLOURS[f.id] ?? 0xe8e6de, roughness: 0.7, metalness: 0,
      polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2,
    });
  } else {
    const slot = SLOT[f.id];
    if (slot) { mat = mats[slot] ??= await slotMaterial(man, slot); if (slot === 'grass') mat.color.setHex(0xc8e2a8); }
    else mat = new THREE.MeshStandardMaterial({ color: 0x808080, roughness: 0.9 });
  }
  const m = new THREE.Mesh(geo, mat);
  m.userData.marking = f.kind === 'marking'; m.castShadow = f.kind !== 'marking'; m.receiveShadow = true;
  group.add(m);
  report.push(f.file + ' -> ' + (f.kind === 'marking' ? 'marking ' + f.id : (SLOT[f.id] || 'grey')));
}
// Ground: grass plane just below the lowest road geometry (also under export_graded terrain, so the horizon is never empty).
{
  const zmin = exportInfo.bounds_min[2];
  const gm = await slotMaterial(man, 'grass');
  gm.color.setHex(0xc8e2a8); // mild viewer tint: the set alone reads brown
  const ground = new THREE.Mesh(new THREE.PlaneGeometry(8000, 8000), gm);
  ground.rotation.x = -Math.PI / 2; ground.position.y = zmin - 0.03;
  const g = ground.geometry; const uv = g.attributes.uv; // PlaneGeometry uv is 0..1: make it metres
  for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * 8000, uv.getY(i) * 8000);
  ground.receiveShadow = true; scene.add(ground);
}

// view: { name, pos:[x,y,z], target:[x,y,z] in sim metres (original sim frame), fov (vertical, deg) or hfov (horizontal, deg),
//         sun: [compass azimuth deg, elevation deg] (default: from the WNW, 38 deg),
//         width, height (pixels; default 1600x900), near, shadow (radius, m), exposure }
window.setView = (v) => {
  const P = a => new THREE.Vector3(...S2T(a[0] - off[0], a[1] - off[1], a[2] - off[2]));
  // rel: z of pos / target is a height above the road surface (highest non-marking mesh) at that xy.
  const ray = new THREE.Raycaster(), down = new THREE.Vector3(0, -1, 0);
  const surf = group.children.filter(m => !m.userData.marking);
  const lift = a => {
    if (!v.rel) return a;
    const p = P([a[0], a[1], 0]); p.y = 200; ray.set(p, down);
    const h = ray.intersectObjects(surf, false)[0];
    return [a[0], a[1], (h ? h.point.y : 0) + a[2] + off[2]];
  };
  if (v.sun) { const az = THREE.MathUtils.degToRad(v.sun[0]), el = THREE.MathUtils.degToRad(v.sun[1]); sunDir.set(Math.cos(el) * Math.sin(az), Math.sin(el), -Math.cos(el) * Math.cos(az)); } else sunDir.copy(sunDir0);
  sky.material.uniforms.sunPosition.value.copy(sunDir);
  const pos = P(lift(v.pos)), tgt = P(lift(v.target));
  W = v.width ?? 1600; H = v.height ?? 900; renderer.setSize(W, H);
  camera.fov = v.hfov ? THREE.MathUtils.radToDeg(2 * Math.atan(Math.tan(THREE.MathUtils.degToRad(v.hfov) / 2) * H / W)) : (v.fov ?? 50);
  camera.near = v.near ?? 0.2; camera.far = v.far ?? 6000; camera.aspect = W / H;
  camera.position.copy(pos); camera.up.set(0, 1, 0); camera.lookAt(tgt); camera.updateProjectionMatrix();
  renderer.toneMappingExposure = v.exposure ?? 0.75;
  // Shadow volume follows the area of interest so texels stay small.
  const c = v.shadowCentre ? P(v.shadowCentre) : tgt, r = v.shadow ?? 120;
  sun.target.position.copy(c); sun.position.copy(c).addScaledVector(sunDir, 800);
  const sc = sun.shadow.camera; sc.left = -r; sc.right = r; sc.top = r; sc.bottom = -r; sc.near = 10; sc.far = 1800;
  sc.updateProjectionMatrix(); sun.target.updateMatrixWorld();
  sky.position.copy(camera.position);
  renderer.render(scene, camera);
  renderer.render(scene, camera);
  return true;
};
// Draws [photo | divider | our render] on one canvas (labels top-left of each half) and returns a PNG data URL.
// Both halves have the photo's height; our render is H pixels high already (setView was called with the photo size).
window.compose = async (photoUrl) => {
  const img = new Image(); img.src = photoUrl; await img.decode();
  const gap = 4, h = img.naturalHeight;
  const c = document.createElement('canvas'); c.width = img.naturalWidth + gap + renderer.domElement.width; c.height = h;
  const g = c.getContext('2d'); g.fillStyle = '#fff'; g.fillRect(0, 0, c.width, c.height);
  g.drawImage(img, 0, 0); g.drawImage(renderer.domElement, img.naturalWidth + gap, 0, renderer.domElement.width, h);
  g.font = 'bold 18px sans-serif'; g.textBaseline = 'top';
  for (const [t, x] of [['reference', 10], ['ours', img.naturalWidth + gap + 10]]) {
    const w = g.measureText(t).width; g.fillStyle = 'rgba(0,0,0,0.6)'; g.fillRect(x - 4, h - 30, w + 8, 24); g.fillStyle = '#fff'; g.fillText(t, x, h - 27);
  }
  return c.toDataURL('image/png');
};
window.snap = () => renderer.domElement.toDataURL('image/png');
window.__info = { cameras: exportInfo.cameras ?? [], report, bounds: [exportInfo.bounds_min, exportInfo.bounds_max], off };
window.__ready = true;
